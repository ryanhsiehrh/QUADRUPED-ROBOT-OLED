/* =============================================================================
 *  Project:   Quadruped Spider Robot Oled 
 *  Author:    Ryan Hsieh
 *  Board:     Arduino Pro Micro (ATmega32U4)
 *  Features:  - USB Serial Command Control
 *             - Software I2C 0.96" OLED Face Display (SDA: A0, SCL: A1)
 *             - 3 Custom Motion Emotes (Side-to-Side, Bounce, Up & Down)
 *             - Non-blocking Animated Face with Eye Blinking
 *             - Additional code comments for better understanding
 * =============================================================================
 *  Original Base Code:
 *  - Author: panerqiang@sunfounder.com (2015-01-27)
 *  - Modified by Regis for spider remote control (2015-09-26)
 * ============================================================================= */

/* Includes ------------------------------------------------------------------*/
#include <Servo.h>        //to define and control servos
#include <FlexiTimer2.h>  //to set a timer to manage all servos
// RegisHsu, remote control
#include <SerialCommand.h>
SerialCommand SCmd;  // The demo SerialCommand object
#include <U8g2lib.h> //for oled screen
//software I2C setup: A1 = SCL, A0 = SDA
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ A1, /* data=*/ A0, /* reset=*/ U8X8_PIN_NONE);

/* Oled screen ----------------------------------------------------------------*/
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 3000; // blink every 3s
const unsigned long blinkDuration = 120;  // 120ms blink time

int currentX = 0;
int state = 0; // 0: Center Pause, 1: Pan Left, 2: Pause Left, 3: Pan Right, 4: Pause Right, 5: Pan Center
unsigned long stateTimer = 0;

// TRACKS SITTING STATE 
bool isSitting = false; 

void drawFace(int xOff, bool isBlinking);

/* Servos --------------------------------------------------------------------*/
//define 12 servos for 4 legs
Servo servo[4][3];
//define servos' ports
const int servo_pin[4][3] = { { 2, 3, 4 }, { 5, 6, 7 }, { 8, 9, 10 }, { 16, 14, 15 } };
/* Size of the robot ---------------------------------------------------------*/
const float length_a = 55;     //femure
const float length_b = 77.5;   //tibia
const float length_c = 27.5;   //coxa
const float length_side = 71;  //length+width
const float z_absolute = -28;  //minimum height
/* Constants for movement ----------------------------------------------------*/
const float z_default = -50, z_up = -30, z_boot = z_absolute;
//standing and clearance height for moving
const float x_default = 62, x_offset = 0;  //coxa to tibia distance (sideways)
const float y_start = 0, y_step = 40;      //step distance
/* variables for movement ----------------------------------------------------*/
volatile float site_now[4][3];     //real-time coordinates of the end of each leg
volatile float site_expect[4][3];  //expected coordinates of the end of each leg
float temp_speed[4][3];            //each axis' speed, needs to be recalculated before each movement
float move_speed;                  //movement speed
float speed_multiple = 1;          //movement speed multiple
const float spot_turn_speed = 4;
const float leg_move_speed = 8;
const float body_move_speed = 3;
const float stand_seat_speed = 1;
volatile int rest_counter;  //+1/0.02s, for automatic rest
//functions' parameter
const float KEEP = 255;
//define PI for calculation
const float pi = 3.1415926;
/* Constants for turn --------------------------------------------------------*/
//temp length
const float temp_a = sqrt(pow(2 * x_default + length_side, 2) + pow(y_step, 2));
//diagonal length from whole x length and y_step(40)
const float temp_b = 2 * (y_start + y_step) + length_side;
//front to back distance w/ body width
const float temp_c = sqrt(pow(2 * x_default + length_side, 2) + pow(2 * y_start + y_step + length_side, 2));
//entire diagonal length from top left to bottom right
const float temp_alpha = acos((pow(temp_a, 2) + pow(temp_b, 2) - pow(temp_c, 2)) / 2 / temp_a / temp_b);
//law of cos for angle of rotation during a step
//site for turn marking 4 leg locations for rotation left/right
const float turn_x1 = (temp_a - length_side) / 2;                        //64
const float turn_y1 = y_start + y_step / 2;                              //20
const float turn_x0 = turn_x1 - temp_b * cos(temp_alpha);                //59.61
const float turn_y0 = temp_b * sin(temp_alpha) - turn_y1 - length_side;  //39.73
/* ---------------------------------------------------------------------------*/

/*
  - setup function
   ---------------------------------------------------------------------------*/
void setup() {
  //start serial for debug
  Serial.begin(9600);
  while (!Serial); // Guard for Pro Micro USB stack
  Serial.println("Robot starts initialization");

  // RegisHsu, remote control
  // Setup callbacks for SerialCommand commands
  // action command 0-6,
  // w 0 1: stand
  // w 0 0: sit
  // w 1 x: forward x step
  // w 2 x: back x step
  // w 3 x: right turn x step
  // w 4 x: left turn x step
  // w 5 x: hand shake x times
  // w 6 x: hand wave x times
  SCmd.addCommand("w", action_cmd);

  SCmd.setDefaultHandler(unrecognized);

  //initialize default parameter
  set_site(0, x_default - x_offset, y_start + y_step, z_boot);  //set_site(0, 62, 40, -28)
  set_site(1, x_default - x_offset, y_start + y_step, z_boot);  //set_site(1, 62, 40, -28)
  set_site(2, x_default + x_offset, y_start, z_boot);           //set_site(2, 62, 0, -28)
  set_site(3, x_default + x_offset, y_start, z_boot);           //set_site(3, 62, 0, -28)
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      site_now[i][j] = site_expect[i][j];  //set now to expected
    }
  }
  //start servo service
  FlexiTimer2::set(20, servo_service);  //checks servo every 20 seeconds
  FlexiTimer2::start();
  Serial.println("Servo service started");
  //initialize servos
  servo_attach();  //sets up all initial servo positions
  Serial.println("Servos initialized");
  Serial.println("Robot initialization Complete");

  //Oled display setup
  u8g2.begin();
  u8g2.setBusClock(400000); 
  lastBlinkTime = millis();
  stateTimer = millis();
}


void servo_attach(void) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      servo[i][j].attach(servo_pin[i][j]);
      //turns on pusle width modulation to the pin to go to initial positon
      delay(100);
    }
  }
}

void servo_detach(void) {
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 3; j++) {
      servo[i][j].detach();
      delay(100);
    }
  }
}
/*
  - loop function
   ---------------------------------------------------------------------------*/

void loop() {
  SCmd.readSerial();

  //Code for OLED display face
  unsigned long now = millis();

  // 1. Blink checker
  if (now - lastBlinkTime >= blinkInterval + blinkDuration) {
    lastBlinkTime = now;
  }
  // If sitting, force eyes closed (isBlinking = true); otherwise check timer
  bool isBlinking = isSitting || (now - lastBlinkTime >= blinkInterval);

  // 2. Moving face (pauses movement if sitting)
  if (!isSitting) {
    switch (state) {
      case 0: // Pause at Center for 4 Seconds
        currentX = 0;
        if (now - stateTimer >= 4000) {
          state = 1; // Start panning Left
        }
        break;

      case 1: // Pan left 
        currentX -= 3;
        if (currentX <= -15) {
          currentX = -15;
          state = 2;
          stateTimer = now;
        }
        break;

      case 2: // Brief pause at eft
        if (now - stateTimer >= 200) {
          state = 3; // Start panning Right
        }
        break;

      case 3: // Pan right 
        currentX += 3;
        if (currentX >= 15) {
          currentX = 15;
          state = 4;
          stateTimer = now;
        }
        break;

      case 4: // Brief pause at right
        if (now - stateTimer >= 200) {
          state = 5; // Return to Center
        }
        break;

      case 5: // Pan back to center 
        currentX -= 3;
        if (currentX <= 0) {
          currentX = 0;
          state = 0; // Stay in center for 4s again
          stateTimer = now;
        }
        break;
    }
  } else {
    // Return eyes to center while sitting
    currentX = 0; 
  }

  drawFace(currentX, isBlinking);
  delay(10);
}


// OLED DISPLAY FACE DRAWING
void drawFace(int xOff, bool isBlinking) {
  u8g2.clearBuffer();

  // YELLOW ZONE (Y: 0 to 15) Eyebrows
  u8g2.drawLine(25 + xOff, 13, 45 + xOff, 8);   
  u8g2.drawLine(25 + xOff, 14, 45 + xOff, 9);   
  u8g2.drawLine(103 + xOff, 13, 83 + xOff, 8); 
  u8g2.drawLine(103 + xOff, 14, 83 + xOff, 9); 

  // BLUE ZONE (Y: 17 to 63) Round Eyes vs. Closed Lines
  if (isBlinking) {
    // Closed eyes (Used during 120ms blinks AND throughout sitting state)
    u8g2.drawHLine(25 + xOff, 36, 20); 
    u8g2.drawHLine(83 + xOff, 36, 20); 
  } else {
    // Open eyes
    u8g2.drawDisc(35 + xOff, 36, 10); 
    u8g2.drawDisc(93 + xOff, 36, 10); 
  }

  u8g2.sendBuffer();
}

// tests out all functions, call in setup or loop to test
void do_test(void) {

  Serial.println("Stand");
  stand();
  delay(2000);
  Serial.println("Step forward");
  step_forward(5);
  delay(2000);
  Serial.println("Step back");
  step_back(5);
  delay(2000);
  Serial.println("Turn left");
  turn_left(5);
  delay(2000);
  Serial.println("Turn right");
  turn_right(5);
  delay(2000);
  Serial.println("Hand wave");
  hand_wave(3);
  delay(2000);
  Serial.println("Hand wave");
  hand_shake(3);
  delay(2000);
  Serial.println("Side to side");
  side_to_side(5);
  delay(2000);
  Serial.println("Bounce");
  bounce(5);
  delay(2000);
  Serial.println("Up and down");
  updown(5);
  delay(2000);

  Serial.println("Sit");
  sit();
  delay(5000);
}

// RegisHsu
// w 0 1: stand
// w 0 0: sit
// w 1 x: forward x step
// w 2 x: back x step
// w 3 x: right turn x step
// w 4 x: left turn x step
// w 5 x: hand shake x times
// w 6 x: hand wave x times

// Ryan Hsieh
// w 7 x: side to side x times
// w 8 x: bounce x times
// w 9 x: up and down x times

#define W_STAND_SIT 0
#define W_FORWARD 1
#define W_BACKWARD 2
#define W_LEFT 3
#define W_RIGHT 4
#define W_SHAKE 5
#define W_WAVE 6
#define W_SIDETOSIDE 7
#define W_BOUNCE 8
#define W_UPDOWN 9

void action_cmd(void) {
  char *arg;
  int action_mode, n_step;
  Serial.println("Action:");
  arg = SCmd.next();
  action_mode = atoi(arg);
  arg = SCmd.next();
  n_step = atoi(arg);

  // Starts up face on any command except sit (w 0 0)
  if (action_mode != W_STAND_SIT || n_step == 1) {
    isSitting = false;
  }

  switch (action_mode) {
    case W_FORWARD:
      Serial.println("Step forward");
      if (!is_stand())
        stand();
      step_forward(n_step);
      break;
    case W_BACKWARD:
      Serial.println("Step back");
      if (!is_stand())
        stand();
      step_back(n_step);
      break;
    case W_LEFT:
      Serial.println("Turn left");
      if (!is_stand())
        stand();
      turn_left(n_step);
      break;
    case W_RIGHT:
      Serial.println("Turn right");
      if (!is_stand())
        stand();
      turn_right(n_step);
      break;
    case W_STAND_SIT:
      Serial.println("1:up,0:dn");
      if (n_step)
        stand();
      else
        sit();
      break;
    case W_SHAKE:
      Serial.println("Hand shake");
      hand_shake(n_step);
      break;
    case W_WAVE:
      Serial.println("Hand wave");
      hand_wave(n_step);
      break;

    case W_SIDETOSIDE:
      Serial.println("Side to side");
      if (!is_stand())
        stand();
      side_to_side(n_step);
      break;
    case W_BOUNCE:
      Serial.println("Bounce");
      if (!is_stand())
        stand();
      bounce(n_step);
      break;
    case W_UPDOWN:
      Serial.println("Up and down");
      if (!is_stand())
        stand();
      updown(n_step);
      break;

    default:
      Serial.println("Error");
      break;
  }
}

// This gets set as the default handler, and gets called when no other command matches.
void unrecognized(const char *command) {
  Serial.println("Unknown command");
}

/*
  - is_stand
   ---------------------------------------------------------------------------*/
bool is_stand(void) {

  if (site_now[0][2] == z_default)
    return true;
  else
    return false;
}

/*
  - sit
  - blocking function
   ---------------------------------------------------------------------------*/
void sit(void) {
  isSitting = true; // Makes eyes closed on display

  move_speed = stand_seat_speed;
  for (int leg = 0; leg < 4; leg++) {
    set_site(leg, KEEP, KEEP, z_boot);  //changes z to sit
  }
  wait_all_reach();  //wait for all legs to reach new position
}

/*
  - stand
  - blocking function
   ---------------------------------------------------------------------------*/
void stand(void) {
  isSitting = false; // Opens eyes and starts panning/blinking
  move_speed = stand_seat_speed;
  for (int leg = 0; leg < 4; leg++) {
    set_site(leg, KEEP, KEEP, z_default);  //changes z to stand
  }
  wait_all_reach();  //wait for all legs to reach new position
}


/*
  - spot turn to left
  - blocking function
  - parameter step steps wanted to turn
   ---------------------------------------------------------------------------*/
void turn_left(unsigned int step) {
  move_speed = spot_turn_speed;
  while (step-- > 0) {
    if (site_now[3][1] == y_start) {
      //leg 3&1 move
      set_site(3, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      set_site(0, turn_x1 - x_offset, turn_y1, z_default);
      set_site(1, turn_x0 - x_offset, turn_y0, z_default);
      set_site(2, turn_x1 + x_offset, turn_y1, z_default);
      set_site(3, turn_x0 + x_offset, turn_y0, z_up);
      wait_all_reach();

      set_site(3, turn_x0 + x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(0, turn_x1 + x_offset, turn_y1, z_default);
      set_site(1, turn_x0 + x_offset, turn_y0, z_default);
      set_site(2, turn_x1 - x_offset, turn_y1, z_default);
      set_site(3, turn_x0 - x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(1, turn_x0 + x_offset, turn_y0, z_up);
      wait_all_reach();

      set_site(0, x_default + x_offset, y_start, z_default);
      set_site(1, x_default + x_offset, y_start, z_up);
      set_site(2, x_default - x_offset, y_start + y_step, z_default);
      set_site(3, x_default - x_offset, y_start + y_step, z_default);
      wait_all_reach();

      set_site(1, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    } else {
      //leg 0&2 move
      set_site(0, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      set_site(0, turn_x0 + x_offset, turn_y0, z_up);
      set_site(1, turn_x1 + x_offset, turn_y1, z_default);
      set_site(2, turn_x0 - x_offset, turn_y0, z_default);
      set_site(3, turn_x1 - x_offset, turn_y1, z_default);
      wait_all_reach();

      set_site(0, turn_x0 + x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(0, turn_x0 - x_offset, turn_y0, z_default);
      set_site(1, turn_x1 - x_offset, turn_y1, z_default);
      set_site(2, turn_x0 + x_offset, turn_y0, z_default);
      set_site(3, turn_x1 + x_offset, turn_y1, z_default);
      wait_all_reach();

      set_site(2, turn_x0 + x_offset, turn_y0, z_up);
      wait_all_reach();

      set_site(0, x_default - x_offset, y_start + y_step, z_default);
      set_site(1, x_default - x_offset, y_start + y_step, z_default);
      set_site(2, x_default + x_offset, y_start, z_up);
      set_site(3, x_default + x_offset, y_start, z_default);
      wait_all_reach();

      set_site(2, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    }
  }
}

/*
  - spot turn to right
  - blocking function
  - parameter step steps wanted to turn
   ---------------------------------------------------------------------------*/
void turn_right(unsigned int step) {
  move_speed = spot_turn_speed;
  while (step-- > 0) {
    if (site_now[2][1] == y_start) {
      //leg 2&0 move

      //lift leg 2
      // Leg 2: X = 62mm,  Y = 0mm,   Z = -30mm (Air)
      set_site(2, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      //shift body weight
      // Leg 0: X = 59.61mm, Y = 39.73mm, Z = -50mm (Ground)
      // Leg 1: X = 64.12mm, Y = 20.00mm, Z = -50mm (Ground)
      // Leg 2: X = 59.61mm, Y = 39.73mm, Z = -30mm (Air)
      // Leg 3: X = 64.12mm, Y = 20.00mm, Z = -50mm (Ground)
      set_site(0, turn_x0 - x_offset, turn_y0, z_default);
      set_site(1, turn_x1 - x_offset, turn_y1, z_default);
      set_site(2, turn_x0 + x_offset, turn_y0, z_up);
      set_site(3, turn_x1 + x_offset, turn_y1, z_default);
      wait_all_reach();

      //plant leg 2
      // Leg 2: X = 59.61mm, Y = 39.73mm, Z = -50mm (Ground)
      set_site(2, turn_x0 + x_offset, turn_y0, z_default);
      wait_all_reach();

      //pivot the body
      // Leg 0: X = 59.61mm, Y = 39.73mm, Z = -50mm (Ground)
      // Leg 1: X = 64.12mm, Y = 20.00mm, Z = -50mm (Ground)
      // Leg 2: X = 59.61mm, Y = 39.73mm, Z = -50mm (Ground)
      // Leg 3: X = 64.12mm, Y = 20.00mm, Z = -50mm (Ground)
      set_site(0, turn_x0 + x_offset, turn_y0, z_default);
      set_site(1, turn_x1 + x_offset, turn_y1, z_default);
      set_site(2, turn_x0 - x_offset, turn_y0, z_default);
      set_site(3, turn_x1 - x_offset, turn_y1, z_default);
      wait_all_reach();

      //lift leg 0
      // Leg 0: X = 59.61mm, Y = 39.73mm, Z = -30mm (Air)
      set_site(0, turn_x0 + x_offset, turn_y0, z_up);
      wait_all_reach();

      //move leg 0
      // Leg 0: X = 62.00mm, Y = 0mm,     Z = -30mm (Air)
      // Leg 1: X = 62.00mm, Y = 0mm,     Z = -50mm (Ground)
      // Leg 2: X = 62.00mm, Y = 40.00mm, Z = -50mm (Ground)
      // Leg 3: X = 62.00mm, Y = 40.00mm, Z = -50mm (Ground)
      set_site(0, x_default + x_offset, y_start, z_up);
      set_site(1, x_default + x_offset, y_start, z_default);
      set_site(2, x_default - x_offset, y_start + y_step, z_default);
      set_site(3, x_default - x_offset, y_start + y_step, z_default);
      wait_all_reach();

      //plant leg 0
      // Leg 0: X = 62.00mm, Y = 0mm,     Z = -50mm (Ground)
      set_site(0, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    } else {
      //leg 1&3 move
      set_site(1, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      set_site(0, turn_x1 + x_offset, turn_y1, z_default);
      set_site(1, turn_x0 + x_offset, turn_y0, z_up);
      set_site(2, turn_x1 - x_offset, turn_y1, z_default);
      set_site(3, turn_x0 - x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(1, turn_x0 + x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(0, turn_x1 - x_offset, turn_y1, z_default);
      set_site(1, turn_x0 - x_offset, turn_y0, z_default);
      set_site(2, turn_x1 + x_offset, turn_y1, z_default);
      set_site(3, turn_x0 + x_offset, turn_y0, z_default);
      wait_all_reach();

      set_site(3, turn_x0 + x_offset, turn_y0, z_up);
      wait_all_reach();

      set_site(0, x_default - x_offset, y_start + y_step, z_default);
      set_site(1, x_default - x_offset, y_start + y_step, z_default);
      set_site(2, x_default + x_offset, y_start, z_default);
      set_site(3, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      set_site(3, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    }
  }
}

/*
  - go forward
  - blocking function
  - parameter step steps wanted to go
   ---------------------------------------------------------------------------*/
void step_forward(unsigned int step) {  //back right, front right, front left, back left
  move_speed = leg_move_speed;
  while (step-- > 0) {
    if (site_now[2][1] == y_start) {
      //leg 2&1 move
      // 1. Lift Leg 2 (Back-Right) straight up
      // Leg 2: X = 62mm,  Y = 0mm,   Z = -30mm (Air)
      set_site(2, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      // 2. Swing Leg 2 forward in the air by a massive 80mm step
      // Leg 2: X = 62mm,  Y = 80mm,  Z = -30mm (Air)
      set_site(2, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();

      // 3. Plant Leg 2 firmly onto the floor
      // Leg 2: X = 62mm,  Y = 80mm,  Z = -50mm (Ground)
      set_site(2, x_default + x_offset, y_start + 2 * y_step, z_default);
      wait_all_reach();

      // 4. THE BODY SHIFT: Slow down speed, drag the chassis forward over the feet
      // Leg 0: X = 62mm,  Y = 0mm,   Z = -50mm (Ground)
      // Leg 1: X = 62mm,  Y = 80mm,  Z = -50mm (Ground)
      // Leg 2: X = 62mm,  Y = 40mm,  Z = -50mm (Ground) (Sign flipping makes it 40mm)
      // Leg 3: X = 62mm,  Y = 40mm,  Z = -50mm (Ground)
      move_speed = body_move_speed;
      set_site(0, x_default + x_offset, y_start, z_default);
      set_site(1, x_default + x_offset, y_start + 2 * y_step, z_default);
      set_site(2, x_default - x_offset, y_start + y_step, z_default);
      set_site(3, x_default - x_offset, y_start + y_step, z_default);
      wait_all_reach();

      // 5. Shift back to fast speed for the next air swing
      move_speed = leg_move_speed;

      // 6. Lift Leg 1 (Front-Right) straight up
      // Leg 1: X = 62mm,  Y = 80mm,  Z = -30mm (Air)
      set_site(1, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();

      // 7. Pull Leg 1 backward through the air to reset it
      // Leg 1: X = 62mm,  Y = 0mm,   Z = -30mm (Air)
      set_site(1, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      // 8. Plant Leg 1 down to complete Phase A
      // Leg 1: X = 62mm,  Y = 0mm,   Z = -50mm (Ground)
      set_site(1, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    } else {
      //leg 0&3 move
      set_site(0, x_default + x_offset, y_start, z_up);
      wait_all_reach();
      set_site(0, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();
      set_site(0, x_default + x_offset, y_start + 2 * y_step, z_default);
      wait_all_reach();

      move_speed = body_move_speed;

      set_site(0, x_default - x_offset, y_start + y_step, z_default);
      set_site(1, x_default - x_offset, y_start + y_step, z_default);
      set_site(2, x_default + x_offset, y_start, z_default);
      set_site(3, x_default + x_offset, y_start + 2 * y_step, z_default);
      wait_all_reach();

      move_speed = leg_move_speed;

      set_site(3, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();
      set_site(3, x_default + x_offset, y_start, z_up);
      wait_all_reach();
      set_site(3, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    }
  }
}

/*
  - go back
  - blocking function
  - parameter step steps wanted to go
   ---------------------------------------------------------------------------*/
void step_back(unsigned int step) {  //back left, front left, front right, back right
  move_speed = leg_move_speed;
  while (step-- > 0) {
    if (site_now[3][1] == y_start) {
      //leg 3&0 move

      // 1. Lift Leg 3 (Back-Left) straight up into the air
      // Leg 3: X = 62mm,  Y = 0mm,   Z = -30mm (Air)
      set_site(3, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      // 2. Extend Leg 3 deep backward through the air by 80mm
      // Leg 3: X = 62mm,  Y = 80mm,  Z = -30mm (Air)
      set_site(3, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();

      // 3. Stomp Leg 3 down onto the ground to anchor the rear left corner
      // Leg 3: X = 62mm,  Y = 80mm,  Z = -50mm (Ground)
      set_site(3, x_default + x_offset, y_start + 2 * y_step, z_default);
      wait_all_reach();

      // ========================================================
      // 4. THE BODY SHIFT: Slow down speed, push the chassis backward
      // Leg 0: X = 62mm,  Y = 80mm,  Z = -50mm (Ground)
      // Leg 1: X = 62mm,  Y = 0mm,   Z = -50mm (Ground)
      // Leg 2: X = 62mm,  Y = 40mm,  Z = -50mm (Ground)
      // Leg 3: X = 62mm,  Y = 40mm,  Z = -50mm (Ground)
      // ========================================================
      move_speed = body_move_speed;
      set_site(0, x_default + x_offset, y_start + 2 * y_step, z_default);
      set_site(1, x_default + x_offset, y_start, z_default);
      set_site(2, x_default - x_offset, y_start + y_step, z_default);
      set_site(3, x_default - x_offset, y_start + y_step, z_default);
      wait_all_reach();

      // 5. Shift back to fast speed for the front leg reset swing
      move_speed = leg_move_speed;

      // 6. Lift Leg 0 (Front-Left) straight up into the air
      // Leg 0: X = 62mm,  Y = 80mm,  Z = -30mm (Air)
      set_site(0, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();

      // 7. Swing Leg 0 forward through the air to untangle it from the body
      // Leg 0: X = 62mm,  Y = 0mm,   Z = -30mm (Air)
      set_site(0, x_default + x_offset, y_start, z_up);
      wait_all_reach();

      // 8. Plant Leg 0 back down on the ground to re-establish a forward anchor
      // Leg 0: X = 62mm,  Y = 0mm,   Z = -50mm (Ground)
      set_site(0, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    } else {
      //leg 1&2 move
      set_site(1, x_default + x_offset, y_start, z_up);
      wait_all_reach();
      set_site(1, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();
      set_site(1, x_default + x_offset, y_start + 2 * y_step, z_default);
      wait_all_reach();

      move_speed = body_move_speed;

      set_site(0, x_default - x_offset, y_start + y_step, z_default);
      set_site(1, x_default - x_offset, y_start + y_step, z_default);
      set_site(2, x_default + x_offset, y_start + 2 * y_step, z_default);
      set_site(3, x_default + x_offset, y_start, z_default);
      wait_all_reach();

      move_speed = leg_move_speed;

      set_site(2, x_default + x_offset, y_start + 2 * y_step, z_up);
      wait_all_reach();
      set_site(2, x_default + x_offset, y_start, z_up);
      wait_all_reach();
      set_site(2, x_default + x_offset, y_start, z_default);
      wait_all_reach();
    }
  }
}

// add by RegisHsu

void body_left(int i) {
  set_site(0, site_now[0][0] + i, KEEP, KEEP); // Left side: Increase X (push away)
  set_site(1, site_now[1][0] + i, KEEP, KEEP); // Right side: Increase X (pull in)
  set_site(2, site_now[2][0] - i, KEEP, KEEP); // Right side: Decrease X (pull in)
  set_site(3, site_now[3][0] - i, KEEP, KEEP); // Left side: Decrease X (push away)
  wait_all_reach();
}

void body_right(int i) {
  set_site(0, site_now[0][0] - i, KEEP, KEEP);
  set_site(1, site_now[1][0] - i, KEEP, KEEP);
  set_site(2, site_now[2][0] + i, KEEP, KEEP);
  set_site(3, site_now[3][0] + i, KEEP, KEEP);
  wait_all_reach();
}

void hand_wave(int i) {
  float x_tmp;
  float y_tmp;
  float z_tmp;
  move_speed = 1;
  if (site_now[3][1] == y_start) {
    body_right(15);
    //save the position of foot about to be raised (2)
    x_tmp = site_now[2][0];
    y_tmp = site_now[2][1];
    z_tmp = site_now[2][2];
    move_speed = body_move_speed;
    for (int j = 0; j < i; j++) { //wave i number of times
      set_site(2, turn_x1, turn_y1, 50);
      wait_all_reach();
      set_site(2, turn_x0, turn_y0, 50);
      wait_all_reach();
    }
    set_site(2, x_tmp, y_tmp, z_tmp); //put foot back down
    wait_all_reach();
    move_speed = 1;
    body_left(15); //shift body back to original
  } else {
    body_left(15);
    x_tmp = site_now[0][0];
    y_tmp = site_now[0][1];
    z_tmp = site_now[0][2];
    move_speed = body_move_speed;
    for (int j = 0; j < i; j++) {
      set_site(0, turn_x1, turn_y1, 50);
      wait_all_reach();
      set_site(0, turn_x0, turn_y0, 50);
      wait_all_reach();
    }
    set_site(0, x_tmp, y_tmp, z_tmp);
    wait_all_reach();
    move_speed = 1;
    body_right(15);
  }
}

void hand_shake(int i) {
  float x_tmp;
  float y_tmp;
  float z_tmp;
  move_speed = 1;
  if (site_now[3][1] == y_start) {
    body_right(15);
    x_tmp = site_now[2][0];
    y_tmp = site_now[2][1];
    z_tmp = site_now[2][2];
    move_speed = body_move_speed;
    for (int j = 0; j < i; j++) { //hand moves up and down i times
      set_site(2, x_default - 30, y_start + 2 * y_step, 55);
      wait_all_reach();
      set_site(2, x_default - 30, y_start + 2 * y_step, 10);
      wait_all_reach();
    }
    set_site(2, x_tmp, y_tmp, z_tmp);
    wait_all_reach();
    move_speed = 1;
    body_left(15);
  } else {
    body_left(15);
    x_tmp = site_now[0][0];
    y_tmp = site_now[0][1];
    z_tmp = site_now[0][2];
    move_speed = body_move_speed;
    for (int j = 0; j < i; j++) {
      set_site(0, x_default - 30, y_start + 2 * y_step, 55);
      wait_all_reach();
      set_site(0, x_default - 30, y_start + 2 * y_step, 10);
      wait_all_reach();
    }
    set_site(0, x_tmp, y_tmp, z_tmp);
    wait_all_reach();
    move_speed = 1;
    body_right(15);
  }
}
//Added emotes below by Ryan Hsieh

/*
  - have robot move side to side 
  - blocking function
*/
void side_to_side(unsigned int step){
  move_speed = 1;

  body_right(15);
  wait_all_reach();

  //move side to side step times
  for( unsigned int i = 0; i < step; i++ ){
    body_left(30);
    wait_all_reach();

    body_right(30);
    wait_all_reach();
  }

  //go back to initial center position
  body_left(15);
  wait_all_reach();
}

/*
  - have robot bouncing its body in a U shape
  - blocking function
*/
void bounce(unsigned int step){
  move_speed = 1;

  //start at top right position
  body_right(15);

  for (int leg = 0; leg < 4; leg++) {
      set_site(leg, KEEP, KEEP, -55);  //changes z to move up
  }
  wait_all_reach(); 

  bool is_on_right = true;

  //draw the U shape step times
  for( unsigned int i = 0; i < step; i++ ){
    
    //going left
    if( is_on_right ){
      body_left(15);
      for (int leg = 0; leg < 4; leg++) {
        set_site(leg, KEEP, KEEP, -30);  //changes z to sit
      }
      wait_all_reach();

      body_left(15);
      for (int leg = 0; leg < 4; leg++) {
        set_site(leg, KEEP, KEEP, -55);  //changes z to move up
      }
      wait_all_reach(); 

      is_on_right = false;
    }
    //going right
    else{
      body_right(15);
      for (int leg = 0; leg < 4; leg++) {
        set_site(leg, KEEP, KEEP, -30);  //changes z to sit
      }
      wait_all_reach();

      body_right(15);
      for (int leg = 0; leg < 4; leg++) {
        set_site(leg, KEEP, KEEP, -55);  //changes z to move up
      }
      wait_all_reach(); 
    
      is_on_right = true;
    }
    
    
  }
  //go back to initial center position
  for (int leg = 0; leg < 4; leg++) {
    set_site(leg, KEEP, KEEP, -30);  //changes z to sit
  }

  //move back to original position
  if( !is_on_right ){
    body_right(15);
  }
  else{
    body_left(15);
  }
  wait_all_reach(); 
}
/*
  - have robot body move up and down 
  - blocking function
*/
void updown(unsigned int step){
  move_speed = stand_seat_speed;
  for( unsigned int i = 0; i < step; i++ ){

    for (int leg = 0; leg < 4; leg++) {
      set_site(leg, KEEP, KEEP, -65);  //changes z to move up
    }
    wait_all_reach();  

    for (int leg = 0; leg < 4; leg++) {
      set_site(leg, KEEP, KEEP, z_boot);  //changes z to sit
    }
    wait_all_reach();
    
  }
}

/*
  - microservos service /timer interrupt function/50Hz
  - when set site expected,this function move the end point to it in a straight line
  - temp_speed[4][3] should be set before set expect site,it make sure the end point
   move in a straight line,and decide move speed.
  - triggered every 20 ms by flexitimer
   ---------------------------------------------------------------------------*/
void servo_service(void) {
  // sei() (Set Global Interrupt Flag) allows other higher-priority interrupts
  // (like serial communication or sensor echoes) to pause THIS function if needed.
  // This prevents your robot's timing from breaking during heavy math.
  sei();
  static float alpha, beta, gamma;

  for (int i = 0; i < 4; i++) { //all 4 legs
    for (int j = 0; j < 3; j++) { //all 3 axes
      if (abs(site_now[i][j] - site_expect[i][j]) >= abs(temp_speed[i][j]))
        site_now[i][j] += temp_speed[i][j]; //continue moving
      else
        site_now[i][j] = site_expect[i][j]; //stop
    }

    cartesian_to_polar(alpha, beta, gamma, site_now[i][0], site_now[i][1], site_now[i][2]);
    polar_to_servo(i, alpha, beta, gamma);
  }

  rest_counter++;
}

/*
  - set one of end points' expect site
  - this founction will set temp_speed[4][3] at same time
  - non - blocking function
   ---------------------------------------------------------------------------*/
void set_site(int leg, float x, float y, float z) {
  float length_x = 0, length_y = 0, length_z = 0;

  //find lengths needed to travel on each axes
  if (x != KEEP)
    length_x = x - site_now[leg][0];
  if (y != KEEP)
    length_y = y - site_now[leg][1];
  if (z != KEEP)
    length_z = z - site_now[leg][2];

  //diagonal distance needed to be travelled (3D)
  float length = sqrt(pow(length_x, 2) + pow(length_y, 2) + pow(length_z, 2));

  //makes it so all legs complete movement at same time
  //unit vector times move speed times multiple
  temp_speed[leg][0] = length_x / length * move_speed * speed_multiple;
  temp_speed[leg][1] = length_y / length * move_speed * speed_multiple;
  temp_speed[leg][2] = length_z / length * move_speed * speed_multiple;

  if (x != KEEP)
    site_expect[leg][0] = x;
  if (y != KEEP)
    site_expect[leg][1] = y;
  if (z != KEEP)
    site_expect[leg][2] = z;
}

/*
  - wait one of end points move to expect site
  - blocking function
   ---------------------------------------------------------------------------*/
void wait_reach(int leg) {
  while (1)
    if (site_now[leg][0] == site_expect[leg][0])
      if (site_now[leg][1] == site_expect[leg][1])
        if (site_now[leg][2] == site_expect[leg][2])
          break;
}

/*
  - wait all of end points move to expect site
  - blocking function
   ---------------------------------------------------------------------------*/
void wait_all_reach(void) {
  for (int i = 0; i < 4; i++)
    wait_reach(i);
}

/*
  - trans site from cartesian to polar
  - mathematical model 2/2
   ---------------------------------------------------------------------------*/
void cartesian_to_polar(volatile float &alpha, volatile float &beta, volatile float &gamma, volatile float x, volatile float y, volatile float z) {
  //calculate w-z degree
  //variables for 2D plane
  float v, w;
  //distance from shoulder to foot on ground from top view(positive)
  w = (x >= 0 ? 1 : -1) * (sqrt(pow(x, 2) + pow(y, 2)));
  //subtrace the coxa length
  v = w - length_c;
  //angle between femure and z axis
  alpha = atan2(z, v) + acos((pow(length_a, 2) - pow(length_b, 2) + pow(v, 2) + pow(z, 2)) / 2 / length_a / sqrt(pow(v, 2) + pow(z, 2)));
  //angle between femur and tibia
  beta = acos((pow(length_a, 2) + pow(length_b, 2) - pow(v, 2) - pow(z, 2)) / 2 / length_a / length_b);
  //calculate x-y-z degree
  //angle between coxa and x axis
  gamma = (w >= 0) ? atan2(y, x) : atan2(-y, -x);
  //trans radian to degree
  alpha = alpha / pi * 180;
  beta = beta / pi * 180;
  gamma = gamma / pi * 180;
}

/*
  - trans site from polar to microservos
  - mathematical model map to fact
  - the errors saved in eeprom will be add
   ---------------------------------------------------------------------------*/
void polar_to_servo(int leg, float alpha, float beta, float gamma) {
  
  // ========================================================
  // MIRRORING AND DIRECTION MAPPING BY LEG PAIR
  // ========================================================
  
  // LEFT SIDE LEGS: Leg 0 (Front-Left) and Leg 3 (Back-Left)
  if (leg == 0) {
    alpha = 90 - alpha; // Inverts the thigh servo orientation relative to the 90° center
    beta = beta;        // Matches the math engine directly
    gamma += 90;        // Shifts the hip servo's center baseline to 90°
  } 
  else if (leg == 1) {  // RIGHT SIDE LEGS: Leg 1 and Leg 2
    alpha += 90;        // Shifts the right thigh basel ineby adding 90°
    beta = 180 - beta;  // Inverts the knee servo direction because it is physically mirrored
    gamma = 90 - gamma; // Inverts the right hip swing direction so "forward" matches across both sides
  } 
  else if (leg == 2) {  // RIGHT SIDE LEGS: Leg 1 and Leg 2
    alpha += 90;        // Mirrors Leg 1 exactly
    beta = 180 - beta;  
    gamma = 90 - gamma; 
  } 
  else if (leg == 3) {  // LEFT SIDE LEGS: Leg 0 (Front-Left) and Leg 3 (Back-Left)
    alpha = 90 - alpha; // Mirrors Leg 0 exactly
    beta = beta;        
    gamma += 90;        
  }

  // ========================================================
  // HARDWARE OUTPUT TO SERVOS
  // ========================================================
  // Writes the finalized, hardware-calibrated angles to the physical pins.
  // [0] = Joint 0 (Hip/Coxa)
  // [1] = Joint 1 (Thigh/Femur)
  // [2] = Joint 2 (Knee/Tibia)
  servo[leg][0].write(alpha);
  servo[leg][1].write(beta);
  servo[leg][2].write(gamma);
}
