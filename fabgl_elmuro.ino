/*
  Created by Fabrizio Di Vittorio (fdivitto2013@gmail.com) - www.fabgl.com
  Copyright (c) 2019-2020 Fabrizio Di Vittorio.
  All rights reserved.

  This file is part of FabGL Library.

  FabGL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  FabGL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with FabGL.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "fabgl.h"

fabgl::PS2Controller PS2Controller;
SoundGenerator       soundGenerator;
SquareWaveformGenerator swg;

#include "controllers.h"
#include "soundchip.h"

#define TOPMARGIN 6
#define SIDEMARGIN 14
#define INITIALDIR (-PI/2.5)
#define INITIALY 179
#define INITIALX 150
#define PADDLES 3

#define MINANGLE (PI/5)

volatile double      objX    = INITIALX;
volatile double      objY    = INITIALY;
static double        objDir  = INITIALDIR;
static double        objVel  =  4;
static constexpr int objSize =  8;
static TaskHandle_t  mainTaskHandle;
static uint32_t      bgParkMillerState = 1;
long offGame = 0;

// just to avoid floating point calculations inside MyDirectDrawVGAController::drawScanline()
volatile int objIntX;
volatile int objIntY;

volatile double oldobjX;
volatile double oldobjY;

int nBricks = 8*12;
bool brickMap[8][12];

int paddlePos = 320/2;
int nPaddles = PADDLES;

bool bAuto = false;

GameControllerMouse      cMouse;
GameControllerKeys       cKeysArrows;

struct MyDirectDrawVGAController : public fabgl::VGADirectController {

  void IRAM_ATTR drawScanline(uint8_t * dest, int scanLine);

} DisplayController;


inline int fastRandom()
{
  bgParkMillerState = (uint64_t)bgParkMillerState * 48271 % 0x7fffffff;
  return bgParkMillerState % 4;
}


void IRAM_ATTR MyDirectDrawVGAController::drawScanline(uint8_t * dest, int scanLine)
{
  auto fgcolor = createRawPixel(RGB222(3, 3, 3));   
  auto white = createRawPixel(RGB222(3, 3, 3));   
  auto black = createRawPixel(RGB222(0, 0, 0));   
  auto bgcolor = createRawPixel(RGB222(0, 0, 0)); 
   
  if( scanLine >= 32 )
  {
    if(scanLine < 32+16)
    {
      bgcolor = createRawPixel(RGB222(1, 0, 0));
      fgcolor = createRawPixel(RGB222(3, 0, 0));
    }
    else if(scanLine < 32+16+16)
    {
      bgcolor = createRawPixel(RGB222(1, 0, 1));
      fgcolor = createRawPixel(RGB222(3, 0, 3));
    }
    else if(scanLine < 32+16+16+16)
    {
      bgcolor = createRawPixel(RGB222(0, 1, 0));     
      fgcolor = createRawPixel(RGB222(0, 3, 0));     
    }
    else if(scanLine < 32+16+16+16+16)
    {
      bgcolor = createRawPixel(RGB222(1, 1, 0));    
      fgcolor = createRawPixel(RGB222(3, 3, 0));     
    }
  }

  auto width = getScreenWidth();

  // fill line with background color
  if( scanLine < TOPMARGIN)
    memset(dest+8, fgcolor, width-16);
  else
  {
    memset(dest, black, 16);
    memset(dest+319-15, black, 16);
    memset(dest+16, bgcolor, width-32);
    for( int ncompta = 8; ncompta < SIDEMARGIN; ncompta++)
    {
      VGA_PIXELINROW(dest, ncompta) = white;
      VGA_PIXELINROW(dest, 319-ncompta) = white;    
    }
  }  

  // fill object with foreground color
  if (scanLine >= objIntY - objSize / 2 && scanLine <= objIntY + objSize / 2) 
  {  
    int dist =   abs(scanLine - objIntY) == 4 ? 1 : 0;
    
    for (int col = (objIntX - objSize / 2)+dist; col < (objIntX + objSize / 2)-dist; ++col)
        VGA_PIXELINROW(dest, col) = fgcolor;          
  }

  if (scanLine >= 184 && scanLine < 192 ) 
  {    
    for (int col = paddlePos - 16 ; col < paddlePos + 16; ++col) 
        VGA_PIXELINROW(dest, col) = white;         
  }
  
  // drawBricks 
  if( scanLine >= 32 && scanLine < (32+16+16+16+16) && scanLine % 8 != 0)
  {
    for( int x = 0; x < 12*3*8; x++)
    {
      if( x%24 != 0 && x%24 != 23 && brickMap[(scanLine-32)/8][x/24])
        VGA_PIXELINROW(dest, x+16) = fgcolor;
    }
  }

  if( scanLine >= 10 && scanLine <= 13 )
    for (int p = 0; p < nPaddles; p++)
      for( int x = 0; x < 10; x++)
        VGA_PIXELINROW(dest, 300 - x - p*14) = white;
  
  if (scanLine == getScreenHeight() - 1) {
    // signal end of screen
    vTaskNotifyGiveFromISR(mainTaskHandle, NULL);
  }
}

void resetGame()
{
  if( nBricks )
    playSoundReset();  
  else 
    playSoundWon();  
  
  memset( brickMap, true, sizeof(brickMap));
  nBricks = 12*8;
  objIntX = objX = INITIALX;
  objIntY = objY    = INITIALY;
  objDir  = INITIALDIR;
  offGame = millis();
  nPaddles = PADDLES;
  
}

void setup()
{
  Serial.begin(115200); delay(500); Serial.write("\n\n\n"); // DEBUG ONLY

  PS2Controller.begin(PS2Preset::KeyboardPort0_MousePort1, KbdMode::GenerateVirtualKeys);
  cMouse = GameControllerMouse (0);
  cKeysArrows = GameControllerKeys (1, fabgl::VK_UP, fabgl::VK_DOWN, fabgl::VK_LEFT, fabgl::VK_RIGHT, fabgl::VK_RSHIFT, fabgl::VK_ESCAPE);

  mainTaskHandle = xTaskGetCurrentTaskHandle();

  DisplayController.begin();
  DisplayController.setResolution(VGA_320x200_75Hz);
  soundGenerator.setVolume(127);
  soundGenerator.play(true);
  soundGenerator.attach( &swg);

  resetGame();
  delay(3000);
}

void setDir( double newDir )
{
  objDir = newDir;
    //normalize dir
  while( objDir > PI) objDir -= PI*2;
  while( objDir < -PI) objDir += PI*2;
}


void loop()
{
  if( millis() - offGame > 4000)
    offGame = false;
  
  // check brick collision
  static int oldbricky = -1;
  int brickx = (objIntX-16)/24;
  int bricky = (objIntY-32)/8;
  
  if( bricky >= 0 && bricky <8 && brickx < 12 && brickMap[bricky][brickx] )
  {
      brickMap[bricky][brickx] = false;
      nBricks--;      
      setDir ((( bricky != oldbricky ) ? 2 : 1) * PI - objDir);    
      playSoundPic(bricky/2);           
  }

  if( nBricks == 0)
    resetGame();

  double dmin = objSize / 2 +SIDEMARGIN;
  double dmax = 320 - objSize / 2-SIDEMARGIN;
  // test collision with borders and bounce changing direction
  if ( (objX < dmin  &&  oldobjX >= dmin ) || (objX > dmax && oldobjX <= dmax ) )
  {
    setDir ( PI - objDir );
    playSoundPong();
  }
  else if (objY < (objSize / 2 +TOPMARGIN) && (oldobjX >= (objSize / 2 +TOPMARGIN))) 
  {
    setDir ( 2 * PI - objDir );    
    playSoundPong();
  }
  
  // check paddle collision  
  if ( !offGame 
      && (objX > paddlePos -(16+objSize) && objX < paddlePos +(16+objSize)  || oldobjX > paddlePos -(16+objSize) && oldobjX < paddlePos +(16+objSize)  )
      && objY > 185-objSize/2 && oldobjY <= 185-objSize/2)
  {    
    setDir (  2 * PI - objDir );      
    setDir ( objDir + (objX - ((double)paddlePos))*((PI/2.0)/32.0) );
    if( objDir > -MINANGLE) objDir = -MINANGLE;
    if( objDir < -(PI-MINANGLE)) objDir = -(PI-MINANGLE);
    playSoundTuc();
  }

  oldobjX = objX;
  oldobjY = objY;
  
  if( !offGame )
  {
    // calculate new coordinates  
    objX += objVel * cos(objDir);
    objY += objVel * sin(objDir);
  }
  else
    objX = paddlePos +4;
    
  if(objY < 7) objY = 8;

  // Ball lost
  if(objY > 200) 
  {
    playSoundOut();
    offGame = millis();
    nPaddles--;
    if( nPaddles == -1)
      resetGame();
    else
    {
      objY = INITIALY;      
      objDir  = INITIALDIR;      
      objX = paddlePos +4;
    }
  }

  // convert object coordinate to integer

  objIntX = objX;
  objIntY = objY;

  oldbricky = bricky;

  cMouse.update();
  paddlePos += cMouse.getDX();
  if( cKeysArrows.isLeft() ) paddlePos -= 8;
  if( cKeysArrows.isRight() ) paddlePos += 8;
  if( cMouse.isButtonA() || cKeysArrows.isButtonA() )
    offGame = false;

  if( cMouse.isButtonB() || cKeysArrows.isButtonB() )
    bAuto = not bAuto;

  if(bAuto)
  {
    if (paddlePos < objX ) paddlePos +=2;
    if (paddlePos > objX ) paddlePos -=2;
    if( offGame ) offGame = false;
  }
  
  if( paddlePos < 32) paddlePos = 32; 
  if( paddlePos > 319-32) paddlePos = 319-32; 

  // wait for vertical sync
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
