

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <regex>

#include "VG/openvg.h"
#include "VG/vgu.h"
#include "fontinfo.h"
#include "shapes.h"

#include <linux/input.h>
#include <fcntl.h>
#include <pthread.h>

#define xHairLines_ROWS 8
int xHairLines[8][4] = {
    10, 15, 20, 15, // lower left
    15, 10, 15, 20,
    -20, 15, -10, 15, // lower right
    -15, 10, -15, 20,
    10, -15, 20, -15, // upper left
    15, -10, 15, -20,
    -20, -15, -10, -15, // upper right
    -15, -10, -15, -20
};

// touch state structure
typedef struct {
    int fd;
    char* evbuff[80];
    VGfloat x, y, z;
    int max_x, max_y;
    void parse(const char *buff) {
        std::regex re( "x:(\\d+),\\s*y:(\\d+),\\s*z:(\\d+)\\s*"  ) ;
        std::cmatch match_results;
    
        x = 0;
        y = 0;
        z = 0;
        
        if( std::regex_match( (const char *)buff, match_results, re ) && match_results.size() >= 3 ) {
            x = std::stoi(match_results[1].str());
            y = std::stoi(match_results[2].str());
            z = std::stoi(match_results[3].str());
        }
    }
} touch_t;

touch_t touch;            // global touch state
int left_count = 0;
int quitState = 0;
#define    CUR_SIZ  16                       // cursor size, pixels beyond centre dot

// evenThread reads from the touch input file
void *eventThread(void *arg) {
    
    // Open touch driver
    if ((touch.fd = open("/tmp/TCfifo", O_RDONLY)) < 0) {
        fprintf(stderr, "Error opening touch!\n");
        quitState = 1;
        return &quitState;
    }
    touch.x = touch.max_x / 2;               //Reset touch
    touch.y = touch.max_y / 2;
    
    while (1) {
        read(touch.fd, &touch.evbuff, sizeof(char[80]));
        touch.parse((const char *)touch.evbuff);
        printf("[%4.0f,%4.0f]\r",touch.x,touch.y);
    }
}

static int cur_sx, cur_sy, cur_w, cur_h;    // cursor location and dimensions
static int cur_saved = 0;    // amount of data saved in cursor image backup

// saveCursor saves the pixels under the touch cursor
void saveCursor(VGImage CursorBuffer, int curx, int cury, int screen_width, int screen_height, int s) {
    int sx, sy, ex, ey;
    
    sx = curx - s;                       // horizontal
    if (sx < 0) {
        sx = 0;
    }
    ex = curx + s;
    if (ex > screen_width) {
        ex = screen_width;
    }
    cur_sx = sx;
    cur_w = ex - sx;
    
    sy = cury - s;                       // vertical
    if (sy < 0) {
        sy = 0;
    }
    ey = cury + s;
    if (ey > screen_height) {
        ey = screen_height;
    }
    cur_sy = sy;
    cur_h = ey - sy;
    
    vgGetPixels(CursorBuffer, 0, 0, cur_sx, cur_sy, cur_w, cur_h);
    cur_saved = cur_w * cur_h;
}

// restoreCursor restores the pixels under the touch cursor
void restoreCursor(VGImage CursorBuffer) {
    if (cur_saved != 0) {
        vgSetPixels(cur_sx, cur_sy, CursorBuffer, 0, 0, cur_w, cur_h);
    }
}

// circleCursor draws a translucent circle as the touch cursor
void circleCursor(int curx, int cury, int width, int height, int s) {
    Fill(100, 0, 0, 0.50);
    Circle(curx, cury, s);
    Fill(0, 0, 0, 1);
    Circle(curx, cury, 2);
}

// touchinit starts the touch event thread
int touchinit(int w, int h) {
    pthread_t inputThread;
    touch.max_x = w;
    touch.max_y = h;
    return pthread_create(&inputThread, NULL, &eventThread, NULL);
}

int main() {
    int width, height, cursorx, cursory, cbsize;
    
    init(&width, &height);                   // Graphics initialization
    cursorx = width / 2;
    cursory = height / 2;
    cbsize = (CUR_SIZ * 2) + 1;
    VGImage CursorBuffer = vgCreateImage(VG_sABGR_8888, cbsize, cbsize, VG_IMAGE_QUALITY_BETTER);
    
    if (touchinit(width, height) != 0) {
        fprintf(stderr, "Unable to initialize the touch\n");
        exit(1);
    }
    Start(width, height);                   // Start the picture
    Background(0, 0, 0);                   // Black background
    Fill(44, 77, 232, 1);                   // Big blue marble
    Circle(width / 2, 0, width);               // The "world"
    Fill(255, 255, 255, 1);                   // White text
    TextMid(width / 2, height / 2, "Screen Calibration", SerifTypeface, width / 15);    // Greetings
    
    // Draw lines
    Stroke(255, 255, 255, 0.5);
    StrokeWidth(2);
    for( int i = 0; i < xHairLines_ROWS; i++) {
        Line(  ( xHairLines[i][0] < 0 ? width + xHairLines[i][0] : xHairLines[i][0] )
             , ( xHairLines[i][1] < 0 ? height + xHairLines[i][1] : xHairLines[i][1] )
             , ( xHairLines[i][2] < 0 ? width + xHairLines[i][2] : xHairLines[i][2] )
             , ( xHairLines[i][3] < 0 ? height + xHairLines[i][3] : xHairLines[i][3] )
             );
    }
    
    End();                           // update picture
    
    
    
    // MAIN LOOP
    while (left_count < 2) {               // Loop until the left touch button pressed & released
        // if the touch moved...
        if (touch.x != cursorx || touch.y != cursory) {
            restoreCursor(CursorBuffer);
            cursorx = touch.x;
            cursory = touch.y;
            saveCursor(CursorBuffer, cursorx, cursory, width, height, CUR_SIZ);
            circleCursor(cursorx, cursory, width, height, CUR_SIZ);
            End();                   // update picture
        }
    }
    restoreCursor(CursorBuffer);               // not strictly necessary as display will be closed
    vgDestroyImage(CursorBuffer);               // tidy up memory
    finish();                       // Graphics cleanup
    exit(0);
}
