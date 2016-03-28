// -*- mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; -*-
//
// This code is public domain
// (but note, that the led-matrix library this depends on is GPL v2)

#include "led-matrix.h"
#include "threaded-canvas-manipulator.h"
#include "graphics.h"

#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/time.h>

#include <gif_lib.h>

#include <vector>
#include <algorithm>

using namespace rgb_matrix;

int bpm = 100;

class CurrentTime {
public:
  static int ms() {
    struct timeval tv;
    if (gettimeofday(&tv, nullptr) != 0)
      return 0;
    return (int) tv.tv_sec * 1000 + (int) (tv.tv_usec / 1000);
  }
};

class Mouse {
public:
  ~Mouse() {
    close();
  }

  bool open() {
    close();
    fd_ = ::open("/dev/input/mouse0", O_RDWR);
    return fd_ >= 0;
  }

  int fd() {
    return fd_;
  }

  void close() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  void update() {
    int ret = ::read(fd_, data_, sizeof(data_));
    if (ret != sizeof(data_)) {
      fprintf(stderr, "read returned %d\n", ret);
    }
  }

  bool isButtonPressed(int button) {
    return data_[0] & (button == 1 ? 0x2 : 0x1);
  }

private:
  int fd_ = -1;
  unsigned char data_[3] {};
};

class GifPlayer : public ThreadedCanvasManipulator {
public:
  GifPlayer(Canvas *m, int scroll_ms = 60, int fade_out_frames = 0xff)
    : ThreadedCanvasManipulator(m),
      scroll_ms_(scroll_ms),
      fade_out_frames_(fade_out_frames) {
  }

  ~GifPlayer() {
    Stop();
    WaitStopped();   // only now it is safe to delete our instance variables.
  }

  bool Load(const char *filename) {
    int error;
    
    gif_ = DGifOpenFileName(filename, &error);
    if (!gif_) {
      fprintf(stderr, "Couldn't open %s\n", filename);
      return false;
    }

    if (DGifSlurp(gif_) != GIF_OK) {
      fprintf(stderr, "DGifSlurp failed\n");
      return false;
    }

    return true;
  }

  void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
    canvas()->SetPixel(x, y, red, green, blue);
  }

  void Run() {
    const int screen_height = canvas()->height();
    const int screen_width = canvas()->width();
    int frame = 0;

    const int margin = (screen_width - gif_->SWidth) / 2;
    const int marginy = (screen_height - gif_->SHeight) / 2;

    bool animating = running();
    int fade_start_ms = 0;

    while (animating) {
      SavedImage *image = &gif_->SavedImages[frame];
      
      const int iwidth = image->ImageDesc.Width;

      ColorMapObject *colorMap = image->ImageDesc.ColorMap;
      if (!colorMap) {
        colorMap = gif_->SColorMap;
      }

      if (!fade_start_ms && !running()) {
        fade_start_ms = CurrentTime::ms();
      }

      float scale = 1.0;

      if (fade_start_ms) {
        const int fade_duration_ms = 2000;
        int fade_progress_ms = CurrentTime::ms() - fade_start_ms;
        if (fade_progress_ms > fade_duration_ms) {
          animating = false;
          scale = 0.0;
        } else {
          scale -= (float) fade_progress_ms / (float) fade_duration_ms;
        }
      }

      for (int y = 0; y < screen_height; ++y) {
        for (int x = 0; x < screen_width; ++x) {
          const GifColorType *c = 0;
          int iy = y - marginy - image->ImageDesc.Top;
          if (iy >= 0 && iy < image->ImageDesc.Height) {
            int i = x - margin - image->ImageDesc.Left;
            if (i >= 0 && i < image->ImageDesc.Width) {
              int colorIndex = image->RasterBits[iy * iwidth + i];
              c = &colorMap->Colors[colorIndex];
            }
          }
          if (c) {
            if (scale != 1.0) {
              float red = c->Red * scale;
              float green = c->Green * scale;
              float blue = c->Blue * scale;
              SetPixel(x, y, (int) red, (int) green, (int) blue);
            } else {            
              SetPixel(x, y, c->Red, c->Green, c->Blue);
            }
          } else {
            SetPixel(x, y, 0, 0, 0);
          }
        }
      }

      if (++frame >= gif_->ImageCount) {
        frame = 0;
      }

      usleep(scroll_ms_ * 1000);
    }
  }

private:
  GifFileType *gif_;
  const int scroll_ms_;
  const int fade_out_frames_;
};


class TextScroller : public ThreadedCanvasManipulator {
public:
  TextScroller(Canvas *m, int scroll_ms = 60, int pause_ms = 2000)
    : ThreadedCanvasManipulator(m),
      scroll_ms_(scroll_ms),
      pause_ms_(pause_ms) {
  }

  ~TextScroller() {
    Stop();
    WaitStopped();   // only now it is safe to delete our instance variables.
  }

  bool Load(const char *filename) {
    return font_.LoadFont(filename);
  }

  void Run() {
    const int screen_height = canvas()->height();
    const int screen_width = canvas()->width();
    const Color color(0xff, 0, 0);

    int y = (screen_height - font_.height()) / 2;
    unsigned int msg_index = 0;

    while (running()) {
      msg_index++;
      if (msg_index > sizeof(messages_)) {
        msg_index = 0;
      }

      int start_x = screen_width;
      int end_x;

      const char* text = messages_[msg_index];
      printf("using text %s\n", text);

      do {
        canvas()->Clear();

        end_x = start_x;
        end_x += rgb_matrix::DrawText(canvas(), font_, start_x, y + font_.baseline(), color, text);
        start_x--;

        usleep(scroll_ms_ * 1000);
      } while (end_x >= 0);

      usleep(pause_ms_ * 1000);
    }
  }

private:
  rgb_matrix::Font font_;
  static const char* messages_[6];
  const int scroll_ms_;
  const int pause_ms_;
};

const char* TextScroller::messages_[] {
  "more is more",
  "i like it",
  "all is in balance  with the dopeness",
  "house  techno  french fries",
  "the beat  the bass  the sound",
  "warm yer body - HERE",
};


class OffscreenCanvas : public Canvas {
public:
  OffscreenCanvas(int width, int height) 
    : width_(width), height_(height) {
      red_ = new unsigned char[width_ * height_];
      green_ = new unsigned char[width_ * height_];
      blue_ = new unsigned char[width_ * height_];
  }

  ~OffscreenCanvas() override {
    delete [] red_;
    delete [] green_;
    delete [] blue_;
  }

  int width() const {
      return width_;
  }

  int height() const {
      return height_;
  }

  // Set pixel at coordinate (x,y) with given color. Pixel (0,0) is the
  // top left corner.
  // Each color is 8 bit (24bpp), 0 black, 255 brightest.
  void SetPixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
    if (x < 0 || x >= width_ || y <= 0 || y >= height_)
      return;
    int offset = y * width_ + x;
    red_[offset] = red;
    green_[offset] = green;
    blue_[offset] = blue;
  }

  // Clear screen to be all black.
  void Clear() {
    memset(red_, 0, width_ * height_);
    memset(green_, 0, width_ * height_);
    memset(blue_, 0, width_ * height_);
  }

  // Fill screen with given 24bpp color.
  void Fill(uint8_t red, uint8_t green, uint8_t blue) {
    //TODO
  }

  int width_;
  int height_;
  unsigned char* red_;
  unsigned char* green_;
  unsigned char* blue_;
};

struct Page {
  const char* text;
  const char* text2;
  int show_time_beats;
  struct Page* next;
};

class TextSequencer : public ThreadedCanvasManipulator {
public:
  TextSequencer(Canvas *m, std::vector<Page*> &pages)
    : ThreadedCanvasManipulator(m),
      pages_(pages),
      offscreen_(m->width(), m->height()) {
  }

  ~TextSequencer() {
    Stop();
    WaitStopped();   // only now it is safe to delete our instance variables.
  }

  bool Load(const char *filename) {
    return font_.LoadFont(filename);
  }

  void drawPage(Page* page, float alpha) {
    const int r = (int) (alpha * 0xff);
    const Color color(r, 0, 0);

    offscreen_.Clear();
    
    int x, y;
    if (page->text2) {
      y = (canvas()->height() - 2 * font_.height()) / 3;
      x = (canvas()->width() - TextWidth(font_, page->text)) / 2;
      rgb_matrix::DrawText(&offscreen_, font_, x, y + font_.baseline(), color, page->text);

      y += y + font_.height();
      x = (canvas()->width() - TextWidth(font_, page->text2)) / 2;
      rgb_matrix::DrawText(&offscreen_, font_, x, y + font_.baseline(), color, page->text2);
    } else {
      y = (canvas()->height() - font_.height()) / 2;
      x = (canvas()->width() - TextWidth(font_, page->text)) / 2;
      rgb_matrix::DrawText(&offscreen_, font_, x, y + font_.baseline(), color, page->text);
    }
  }

  void present() {
    const int screen_height = canvas()->height();
    const int screen_width = canvas()->width();
    unsigned char* r = offscreen_.red_;
    unsigned char* g = offscreen_.green_;
    unsigned char* b = offscreen_.blue_;

    for (int y = 0 ; y < screen_height; y++) {
      for (int x = 0; x < screen_width; x++) {
        canvas()->SetPixel(x, y, *r++, *g++, *b++);
      }
    }
  }

  void Run() {
    unsigned int index = 0;

    while (running()) {

      // Choose a sequence randomly
      index += 1;
      if (index >= pages_.size()) {
        index = 0;
      }

      for (Page* page = pages_[index]; page; page = page->next) {
        // fade in
        const int fade_duration_ms = 1000 * 1 * 60 / bpm;
        int start_ms = CurrentTime::ms();
        float alpha = 0;
        while (alpha < 1.0) {
          alpha = std::min((float) 1.0, (float) (CurrentTime::ms() - start_ms) / (float) fade_duration_ms);
          drawPage(page, alpha);
          present();
        }

        usleep(page->show_time_beats * 1000000 * 60 / bpm);

        // fade out
        start_ms = CurrentTime::ms();
        while (alpha > 0) {
          alpha = std::max(0.0, 1.0 - (float) (CurrentTime::ms() - start_ms) / (float) fade_duration_ms);
          drawPage(page, alpha);
          present();
        }
      }
    }
  }

private:
  rgb_matrix::Font font_;
  std::vector<Page*> &pages_;
  OffscreenCanvas offscreen_;
};

static int usage(const char *progname) {
  fprintf(stderr, "usage: %s <options> -D <demo-nr> [optional parameter]\n",
          progname);
  fprintf(stderr, "Options:\n"
          "\t-r <rows>     : Display rows. 16 for 16x32, 32 for 32x32. "
          "Default: 32\n"
          "\t-c <chained>  : Daisy-chained boards. Default: 1.\n"
          "\t-L            : 'Large' display, composed out of 4 times 32x32\n"
          "\t-p <pwm-bits> : Bits used for PWM. Something between 1..11\n"
          "\t-l            : Don't do luminance correction (CIE1931)\n"
          "\t-D <demo-nr>  : Always needs to be set\n"
          "\t-d            : run as daemon. Use this when starting in\n"
          "\t                /etc/init.d, but also when running without\n"
          "\t                terminal (e.g. cron).\n"
          "\t-t <seconds>  : Run for these number of seconds, then exit.\n"
          "\t       (if neither -d nor -t are supplied, waits for <RETURN>)\n"
          "\t-w <count>    : Wait states (to throttle I/O speed)\n");
  fprintf(stderr, "Demos, choosen with -D\n");
  fprintf(stderr, "\t0  - some rotating square\n"
          "\t1  - forward scrolling an image (-m <scroll-ms>)\n"
          "\t2  - backward scrolling an image (-m <scroll-ms>)\n"
          "\t3  - test image: a square\n"
          "\t4  - Pulsing color\n"
          "\t5  - Grayscale Block\n"
          "\t6  - Abelian sandpile model (-m <time-step-ms>)\n"
          "\t7  - Conway's game of life (-m <time-step-ms>)\n"
          "\t8  - Langton's ant (-m <time-step-ms>)\n"
          "\t9  - Volume bars (-m <time-step-ms>)\n");
  fprintf(stderr, "Example:\n\t%s -t 10 -D 1 runtext.ppm\n"
          "Scrolls the runtext for 10 seconds\n", progname);
  return 1;
}

bool running = true;

void sigint_handler(int) {
  running = false;
}

int main(int argc, char *argv[]) {
  bool as_daemon = false;
  //int runtime_seconds = -1;
  int demo = 0;
  int rows = 32;
  int chain = 2;
  //int scroll_ms = 30;
  int pwm_bits = -1;
  bool do_luminance_correct = true;
  uint8_t w = 0; // Use default # of write cycles

  int opt;
  while ((opt = getopt(argc, argv, "dlD:t:r:p:c:m:w:L")) != -1) {
    switch (opt) {
    case 'D':
      demo = atoi(optarg);
      break;

    case 'd':
      as_daemon = true;
      break;

    // case 't':
    //   runtime_seconds = atoi(optarg);
    //   break;

    case 'r':
      rows = atoi(optarg);
      break;

    case 'c':
      chain = atoi(optarg);
      break;

    // case 'm':
    //   scroll_ms = atoi(optarg);
    //   break;

    case 'p':
      pwm_bits = atoi(optarg);
      break;

    case 'l':
      do_luminance_correct = !do_luminance_correct;
      break;

    case 'w':
      w = atoi(optarg);
      break;

    default: /* '?' */
      return usage(argv[0]);
    }
  }

  // if (optind < argc) {
  //   demo_parameter = argv[optind];
  // }

  if (demo < 0) {
    fprintf(stderr, "Expected required option -D <demo>\n");
    return usage(argv[0]);
  }

  if (rows != 16 && rows != 32) {
    fprintf(stderr, "Rows can either be 16 or 32\n");
    return 1;
  }

  if (chain < 1) {
    fprintf(stderr, "Chain outside usable range\n");
    return 1;
  }
  if (chain > 8) {
    fprintf(stderr, "That is a long chain. Expect some flicker.\n");
  }

  Canvas *canvas = 0;

  if (getuid() != 0) {
    fprintf(stderr, "Must run as root to be able to access /dev/mem\n"
            "Prepend 'sudo' to the command:\n\tsudo %s ...\n", argv[0]);
    return 1;
  }

  // Initialize GPIO pins. This might fail when we don't have permissions.
  GPIO io;
  if (!io.Init())
    return 1;
  if(w) io.writeCycles = w;

  // Start daemon before we start any threads.
  if (as_daemon) {
    if (fork() != 0)
      return 0;
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
  }

  signal(SIGINT, sigint_handler);

  Mouse mouse;
  if (!mouse.open()) {
    fprintf(stderr, "Couldn't open mouse");
  }

  std::vector<Page*> pages;
  pages.push_back(new Page { "more", "is more", 6, nullptr } );
  pages.push_back(new Page { "i like", "it", 6, nullptr } );
  pages.push_back(new Page { "house", nullptr, 3, 
    new Page { "techno", nullptr, 3,
      new Page { "french", "fries", 3 }}} );
  pages.push_back(new Page { "lovely", nullptr, 6, nullptr } );
  pages.push_back(new Page { "gnarly", nullptr, 6, nullptr } );

#if 0  
  pages.push_back(new Page { "the", "beat", 2000, 
    new Page { "the", "bass", 2000,
      new Page { "the", "sound" }}} );
  pages.push_back(new Page { "warm yer", "body", 2000, 
    new Page { "HERE", nullptr, 2000 }});
  pages.push_back(new Page { "boots", nullptr, 2000, 
    new Page { "cats", nullptr, 2000, 
      new Page { "boots", nullptr, 2000, 
        new Page { "cats", nullptr, 2000, 
          new Page { "boots", nullptr, 2000, 
            new Page { "cats", nullptr, 2000, 
              new Page { "boots", nullptr, 2000, 
                new Page { "cats", nullptr, 2000, }}}}}}}});      
#endif

  // The matrix, our 'frame buffer' and display updater.
  RGBMatrix *matrix = new RGBMatrix(&io, rows, chain);
  matrix->set_luminance_correct(do_luminance_correct);
  if (pwm_bits >= 0 && !matrix->SetPWMBits(pwm_bits)) {
    fprintf(stderr, "Invalid range of pwm-bits\n");
    return 1;
  }

  canvas = matrix;

  enum Mode {
    Idle,
    SpinningHeart,
    Messages,
  };

  Mode mode = demo == 0 ? SpinningHeart : Messages;

  bool b0_pressed = false;
  bool b1_pressed = false;

  // The ThreadedCanvasManipulator objects are filling
  // the matrix continuously.
  ThreadedCanvasManipulator *image_gen = NULL;

  while (running) {

    if (!image_gen) {
      switch (mode) {
      case Idle:
        break;

      case SpinningHeart: {
          GifPlayer* gif_player = new GifPlayer(canvas);
          if (!gif_player->Load("img/rotating_heart.gif")) {
            return 1;
          }
          image_gen = gif_player;
        } 
        break;

      case Messages: {
          TextSequencer *sequencer = new TextSequencer(canvas, pages);
          if (!sequencer->Load("fonts/m12.bdf")) {
            fprintf(stderr, "Couldn't load font\n");
            return 1;
          }
          image_gen = sequencer;
        }
        break;
      }

      if (image_gen)
        // Image generating demo is crated. Now start the thread.
        image_gen->Start();
    }

    // Now, the image generation runs in the background. We can do arbitrary
    // things here in parallel. In this demo, we're essentially just
    // waiting for one of the conditions to exit.
    if (mouse.fd() >= 0) {
      fd_set set;
      FD_ZERO(&set);
      FD_SET(mouse.fd(), &set);
      int ret = select(FD_SETSIZE, &set, NULL, NULL, NULL);
      if (ret > 0 && FD_ISSET(mouse.fd(), &set)) {
        mouse.update();
      }
      if (ret < 0) {
        printf("Got ret %d\n", ret);
      }
    }

    if (mouse.isButtonPressed(0)) {
      b0_pressed = true;
    } else if (b0_pressed) {
      // Idle goes to scrolling, otherwise toggle
      b0_pressed = false;
      delete image_gen;
      image_gen = nullptr;
      switch (mode) {
      case Idle:
      case SpinningHeart:
        mode = Messages;
        break;
      case Messages:
        mode = SpinningHeart;
        break;
      }
    }

    if (mouse.isButtonPressed(1)) {
      b1_pressed = true;
    } else if (b1_pressed) {
      b1_pressed = false;
      delete image_gen;
      image_gen = nullptr;
      // Idle goes to spinning, otherwise go idle.
      mode = (mode == Idle) ? SpinningHeart : Idle;
    }
  }

  printf("Cleaning up and exiting\n");

  delete canvas;

  return 0;
}
