/**
 * @file launch.c
 * @author Akihiro Kiuchi
 * @brief
 * @version 0.1
 * @date 2022-06-15
 *
 * launcherを別に用意するタイプの起動は優先度を低くしたので、このファイルは途中だが、開発を止める。
 */

#define _GNU_SOURCE

#include "launch.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>
#include <poll.h>
#include <pwd.h>
#include <security/pam_appl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/signal.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <systemd/sd-login.h>
#include <unistd.h>

#define DRM_MAJOR 226

#ifndef KDSKBMUTE
#define KDSKBMUTE 0x4B51
#endif

#ifndef EVIOCREVOKE
#define EVIOCREVOKE _IOW('E', 0x91, int)
#endif

struct zippo_launch {
  char* user;  // root only
  char* tty_path;

  struct passwd* pw;
  struct pam_conv pc;
  pam_handle_t* ph;
  int tty;
  int ttynr;
  int kb_mode;

  int sock[2];
  int signal_fd;

  pid_t child;
};

#define DEBUG

static int
open_tty_by_number(int ttynr)
{
  int ret;
  char filename[16];

  ret = snprintf(filename, sizeof filename, "/dev/tty%d", ttynr);
  if (ret < 0) return -1;

  return open(filename, O_RDWR | O_NOCTTY);
}

static int
zippo_launch_setup_signal(struct zippo_launch* self)
{
  int ret;
  sigset_t mask;
  struct sigaction sa;

  memset(&sa, 0, sizeof sa);

  // for SIGCHLD
  sa.sa_handler = SIG_DFL;                  // default
  sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;  // man sigaction
  ret = sigaction(SIGCHLD, &sa, NULL);

  sa.sa_handler = SIG_IGN;  // ignore
  sa.sa_flags = 0;
  sigaction(SIGHUP, &sa, NULL);  // 端末のハング や 制御しているプロセスの死など

  ret = sigemptyset(&mask);
  assert(ret == 0);
  sigaddset(
      &mask, SIGCHLD);  // え、default じゃないの？ -> flag 設定したかったのか
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  // signalfd するときは、そのときのsignalをblockしておくべきらしい
  // `man signalfd`
  ret = sigprocmask(SIG_BLOCK, &mask, NULL);
  assert(ret == 0);

  self->signal_fd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
  if (self->signal_fd < 0) {
    fprintf(stderr, "Failed to create signal fd: %s\n", strerror(errno));
    return -1;
  }

  return 0;
}

static void
zippo_launch_teardown_signal(struct zippo_launch* self)
{
  sigset_t mask;
  struct sigaction sa;

  memset(&sa, 0, sizeof sa);

  sa.sa_handler = SIG_DFL;
  sa.sa_flags = 0;
  sigaction(SIGCHLD, &sa, NULL);

  sa.sa_handler = SIG_DFL;
  sa.sa_flags = 0;
  sigaction(SIGHUP, &sa, NULL);

  sigemptyset(&mask);
  sigaddset(
      &mask, SIGCHLD);  // え、default じゃないの？ -> flag 設定したかったのか
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGTERM);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sigprocmask(SIG_UNBLOCK, &mask, NULL);

  close(self->signal_fd);
}

static int
zippo_launch_setup_launch_socket(struct zippo_launch* self)
{
  if (socketpair(AF_LOCAL, SOCK_SEQPACKET, 0, self->sock) < 0) {
    fprintf(stderr, "Failed to initialize socket pair: %s\n", strerror(errno));
    return -1;
  }

  if (fcntl(self->sock[0], F_SETFD, FD_CLOEXEC) < 0) {
    fprintf(stderr, "fcntl failed: %s\n", strerror(errno));
    close(self->sock[0]);
    close(self->sock[1]);
    return -1;
  }

  return 0;
}

static void
zippo_launch_teardown_launch_socket(struct zippo_launch* self)
{
  close(self->sock[0]);
  close(self->sock[1]);
}

static int
pam_conversation_fn(int msg_count, const struct pam_message** messages,
    struct pam_response** response, void* user_data)
{
  (void)response;
  (void)user_data;

  for (int i = 0; i < msg_count; i++) {
    fprintf(stderr, "%s\n", messages[i]->msg);
  }

  return PAM_SUCCESS;
}

static int
zippo_launch_setup_pam(struct zippo_launch* self)
{
  int ret;

  if (!self->user) return 0;

  self->pc.conv = pam_conversation_fn;
  self->pc.appdata_ptr = self;

  ret = pam_start("login", self->pw->pw_name, &self->pc, &self->ph);
  if (ret != PAM_SUCCESS) {
    fprintf(stderr, "Failed to start pam transaction: %d: %s\n", ret,
        pam_strerror(self->ph, ret));
    goto err;
  }

  ret = pam_set_item(self->ph, PAM_TTY, ttyname(self->tty));
  if (ret != PAM_SUCCESS) {
    fprintf(stderr, "Failed to set PAM_TTY item: %d: %s\n", ret,
        pam_strerror(self->ph, ret));
    goto err_pam;
  }

  ret = pam_open_session(self->ph, 0);
  if (ret != PAM_SUCCESS) {
    fprintf(stderr, "Failed to open pam session: %d: %s\n", ret,
        pam_strerror(self->ph, ret));
    goto err_pam;
  }

#ifdef DEBUG
  fprintf(stderr, "[DEBUG] pam session opened: %d\n", ret);
#endif

  return 0;

err_pam:
  if (pam_end(self->ph, ret) != PAM_SUCCESS)
    fprintf(stderr, "Failed to end pam transaction\n");

err:
  return -1;
}

static void
zippo_launch_teardown_pam(struct zippo_launch* self)
{
  int ret;

  if (!self->user) return;

  ret = pam_close_session(self->ph, 0);
  if (ret != PAM_SUCCESS) {
    fprintf(stderr, "Failed to close pam session: %d: %s\n", ret,
        pam_strerror(self->ph, ret));
  }

  if (pam_end(self->ph, ret) != PAM_SUCCESS)
    fprintf(stderr, "Failed to end pam transaction\n");
}

static int
zippo_launch_setup_tty(struct zippo_launch* self)
{
  struct stat buf;
  struct vt_mode mode = {0};
  char* t;

  if (!self->user) {  // getty
    self->tty = STDIN_FILENO;
  } else if (self->tty_path) {  // root
    t = ttyname(STDIN_FILENO);
    if (t && strcmp(t, self->tty_path) == 0) {
      self->tty = STDIN_FILENO;
    } else {
      // O_NOCTTY pathname が端末 (terminal)デバイス --- tty(4) 参照 ---
      // を指している場合に、たとえそのプロセスが制御端末を持っていなくても、オープンしたファイルは制御端末にはならない。
      self->tty = open(self->tty_path, O_RDWR | O_NOCTTY);
    }
  } else {  // root with no tty specified
    int tty0 = open("/dev/tty0", O_WRONLY | O_CLOEXEC);

    if (tty0 < 0) {
      fprintf(stderr, "Could not open tty0: %s\n", strerror(errno));
      return -1;
    }

    if (ioctl(tty0, VT_OPENQRY, &self->ttynr) < 0 || self->ttynr == -1) {
      fprintf(
          stderr, "Failed to find non-opened console: %s\n", strerror(errno));
      close(tty0);
      return -1;
    }

    self->tty = open_tty_by_number(self->ttynr);
    close(tty0);
  }

  if (self->tty < 0) {
    fprintf(stderr, "Failed to open tty: %s\n", strerror(errno));
    goto err_tty;
  }

  // tty must be tty but not tty0
  if (fstat(self->tty, &buf) == -1 || major(buf.st_rdev) != TTY_MAJOR ||
      minor(buf.st_rdev) == 0) {
    fprintf(stderr, "Must be run from a virtual terminal\n");
    goto err_tty_fstat;
  }

  if (!self->user || self->tty_path) self->ttynr = minor(buf.st_rdev);

#ifdef DEBUG
  fprintf(stderr, "[DEBUG] setup_tty: /dev/tty%d\n", self->ttynr);
#endif

  // virtual terminal を切り替え (画面が切り替わる)
  if (ioctl(self->tty, VT_ACTIVATE, self->ttynr) < 0) {
    fprintf(stderr, "Failed to activate vt: %s\n", strerror(errno));
    goto err_tty_fstat;
  }

  if (ioctl(self->tty, VT_WAITACTIVE, self->ttynr) < 0) {
    fprintf(
        stderr, "Failed to wait for vt to be activate: %s\n", strerror(errno));
    goto err_tty_fstat;
  }

  if (ioctl(self->tty, KDGKBMODE, &self->kb_mode)) {
    fprintf(
        stderr, "Failed to get current keyboard mode: %s\n", strerror(errno));
    goto err_tty_fstat;
  }

#ifdef DEBUG
  const char* kbmodes[] = {"K_RAW", "K_XLATE", "K_MEDIUMRAW", "K_UNICODE"};
  fprintf(stderr, "[DEBUG] kb_mode: %s\n", kbmodes[self->kb_mode]);
#endif

  // 仮想端末でこれをやって、元に戻さないまま死ぬとkeyを受け付けなくなる。
  // その場合はsshとかで、getty@tty<id> をrestart
  if (ioctl(self->tty, KDSKBMUTE, 1) && ioctl(self->tty, KDSKBMODE, K_OFF)) {
    fprintf(stderr, "Failed to set K_OFF keyboard mode: %s", strerror(errno));
    goto err_tty_fstat;
  }

  if (ioctl(self->tty, KDSETMODE, KD_GRAPHICS)) {
    fprintf(
        stderr, "Failed to set KD_GRAPHICS mode on tty: %s\n", strerror(errno));
    goto err_kd_graphics;
  }

  mode.mode = VT_PROCESS;
  mode.relsig = SIGUSR1;
  mode.acqsig = SIGUSR2;
  if (ioctl(self->tty, VT_SETMODE, &mode) < 0) {
    fprintf(
        stderr, "Failed to take control of vt handling: %s\n", strerror(errno));
    goto err_vt_setmode;
  }

  return 0;

err_vt_setmode:
  if (ioctl(self->tty, KDSETMODE, KD_TEXT))
    fprintf(stderr, "Failed to set KD_TEXT mode on tty: %s\n", strerror(errno));

err_kd_graphics:
  if (ioctl(self->tty, KDSKBMUTE, 0) &&
      ioctl(self->tty, KDSKBMODE, self->kb_mode))
    fprintf(stderr, "Failed to restore keyboard mode: %s\n", strerror(errno));

err_tty_fstat:
  if (self->tty != STDIN_FILENO) close(self->tty);

err_tty:
  return -1;
}

static void
zippo_launch_teardown_tty(struct zippo_launch* self)
{
  struct vt_mode mode = {0};
  int oldtty;

  oldtty = self->tty;
  self->tty = open_tty_by_number(self->ttynr);
  close(oldtty);

  if (ioctl(self->tty, KDSKBMUTE, 0) &&
      ioctl(self->tty, KDSKBMODE, self->kb_mode))
    fprintf(stderr, "Failed to restore keyboard mode: %s\n", strerror(errno));

  if (ioctl(self->tty, KDSETMODE, KD_TEXT))
    fprintf(stderr, "Failed to set KD_TEXT mode on tty: %s\n", strerror(errno));

  mode.mode = VT_AUTO;
  if (ioctl(self->tty, VT_SETMODE, &mode) < 0)
    fprintf(stderr, "Failed to reset vt handling: %s\n", strerror(errno));

  if (self->tty != STDIN_FILENO) close(self->tty);
}

// westonでは、以下の場合にtrueを返していた。
// 1. rootユーザである。
// 2. 実行ユーザがweston-launch グループに属している。
// 3. HAVE_SYSTEMD_LOGIN マクロが define
// され、プロセスのsessionがアクティブであり、seatが取得できる。
// ここではHAVE_SYSTEMD_LOGIN マクロはdefineされたものとし、2以外を実装する。
//
// getty環境ではtrueが返るが、ssh環境や、X上の端末で起動した場合はfalse
static bool
zippo_launch_check_permission(struct zippo_launch* self)
{
  (void)self;
  char *session, *seat;
  int err;

  if (getuid() == 0) return true;

  err = sd_pid_get_session(getpid(), &session);
  if (err == 0 && session) {
    if (sd_session_is_active(session) &&
        sd_session_get_seat(session, &seat) == 0) {
#ifdef DEBUG
      fprintf(stderr, "[DEBUG] session: %s, seat: %s\n", session, seat);
#endif
      free(seat);
      free(session);
      return true;
    }
  }

  fprintf(stderr, "Permission denied.\n");

  return false;
}

static int
zippo_launch_set_pw(struct zippo_launch* self)
{
  if (self->user)
    self->pw = getpwnam(self->user);
  else
    self->pw = getpwuid(getuid());

  if (self->pw == NULL) {
    fprintf(stderr, "Failed to get username: %s \n", strerror(errno));
    return -1;
  }

  return 0;
}

static int
zippo_launch_send_reply(struct zippo_launch* self, int reply)
{
  int len;

  do {
    len = send(self->sock[0], &reply, sizeof reply, 0);
  } while (len < 0 && errno == EINTR);

  return len;
}

static void
zippo_launch_handle_socket_msg(struct zippo_launch* self)
{
  (void)self;
  // char control[CMSG_SPACE(sizeof(int))];
  // char buf[BUFSIZ];
  // struct msghdr msg;
  // struct iovec iov;
  // int ret = -1;
  // ssize_t len;
  // struct zippo_launch_message* message;

  // memset(&msg, 0, sizeof(msg));
}

// return >= 0 to exit
static int
zippo_launch_handle_signal(struct zippo_launch* self)
{
  struct signalfd_siginfo sig;
  int pid, status, ret;

  if (read(self->signal_fd, &sig, sizeof sig) != sizeof sig) {
    fprintf(stderr, "Failed to read signal fd: %s\n", strerror(errno));
    return EXIT_FAILURE;
  }

  switch (sig.ssi_signo) {
    case SIGCHLD:
      pid = waitpid(-1, &status, 0);  // wait a child precess to die.
      if (pid == self->child) {
        self->child = 0;
        if (WIFEXITED(status)) {
          ret = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
          // citing from weston:
          /*
           * If weston dies because of signal N, we
           * return 10+N. This is distinct from
           * weston-launch dying because of a signal
           * (128+N).
           */
          ret = 10 + WTERMSIG(status);
        } else {
          ret = 0;
        }
        assert(ret >= 0);
        return ret;
      }
      break;
    case SIGTERM:  // fall through
    case SIGINT:   // fall through
      if (!self->child) break;

      kill(self->child, sig.ssi_signo);
      break;
    case SIGUSR1:
      zippo_launch_send_reply(self, ZIPPO_LAUNCH_DEACTIVATE);
      break;
    case SIGUSR2:
      ioctl(self->tty, VT_RELDISP, VT_ACKACQ);
      // TODO:
      break;
    default:
      assert(0 && "cannot be reached");
  }

  return -1;
}

static void
zippo_launch_compositor_launch(
    struct zippo_launch* self, int argc, char* argv[])
{
  (void)self;
  (void)argc;
  (void)argv;

  // FIXME:
  char* child_argv[3] = {"/bin/uname", "-a", NULL};
  execv(child_argv[0], child_argv);
  fprintf(stderr, "exec failed: %s", strerror(errno));
  exit(EXIT_FAILURE);
}

int
zippo_launch_launch(struct zippo_launch* self, int argc, char* argv[])
{
  int status = -1;

  if (zippo_launch_setup_tty(self) != 0) goto err;

  if (zippo_launch_setup_pam(self) != 0) goto err_setup_pam;

  if (zippo_launch_setup_launch_socket(self) != 0) goto err_setup_launch_sock;

  if (zippo_launch_setup_signal(self) != 0) goto err_setup_signal;

  self->child = fork();
  if (self->child == -1) {
    fprintf(stderr, "Failed to create fork: %s\n", strerror(errno));
    goto err_fork;
  }

  if (self->child == 0)
    zippo_launch_compositor_launch(self, argc, argv);  // -> exit

  close(self->sock[1]);

  while (1) {
    struct pollfd fds[2];
    int n;

    fds[0].fd = self->sock[0];
    fds[0].events = POLLIN;
    fds[1].fd = self->signal_fd;
    fds[1].events = POLLIN;

    n = poll(fds, 2, -1);
    if (n < 0) {
      fprintf(stderr, "poll failed: %s\n", strerror(errno));
      goto err_loop;
    }

    if (fds[0].revents & POLLIN) zippo_launch_handle_socket_msg(self);
    if (fds[1].revents) {
      int ret = zippo_launch_handle_signal(self);
      if (ret >= 0) {
        status = ret;
        break;
      }
    }
  }

err_loop:
err_fork:
  zippo_launch_teardown_signal(self);

err_setup_signal:
  zippo_launch_teardown_launch_socket(self);

err_setup_launch_sock:
  zippo_launch_teardown_pam(self);

err_setup_pam:
  zippo_launch_teardown_tty(self);

err:
  return status;
}

struct zippo_launch*
zippo_launch_create(const char* user, const char* tty)
{
  struct zippo_launch* self;

  self = calloc(1, sizeof *self);
  self->user = user ? strdup(user) : NULL;
  self->tty_path = tty ? strdup(tty) : NULL;

  if (zippo_launch_set_pw(self) != 0) goto err;

  if (!zippo_launch_check_permission(self)) goto err;

  return self;

err:
  free(self->user);
  free(self->tty_path);
  free(self);

  return NULL;
}

void
zippo_launch_destroy(struct zippo_launch* self)
{
  free(self->user);
  free(self->tty_path);
  free(self);
}
