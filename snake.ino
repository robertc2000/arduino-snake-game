#include <Adafruit_NeoPixel.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h> 

#define LED_MATRIX_PIN 6
#define NUMPIXELS 64
#define NR_ROWS 8

Adafruit_NeoPixel pixels(NUMPIXELS, LED_MATRIX_PIN, NEO_GRB + NEO_KHZ800);
LiquidCrystal_I2C lcd(0x27, 16, 2);

#define RUNNING 0
#define GAME_OVER 1

#define UP 0
#define DOWN 1
#define LEFT 2
#define RIGHT 3

#define VERTICAL A0
#define HORIZONTAL A1

#define JOYSTICK_UPPER_THRESHOLD 800
#define JOYSTICK_LOWER_THRESHOLD 200

uint8_t snake_body[NUMPIXELS];
uint8_t prey;
uint8_t snake_dim = 1;
bool must_compute_next_move = 1;
uint8_t game_state;
int was_read = 0;
bool leds_on;
uint8_t score = 0;

#define MAX_LEVEL 3
int current_level = 0;

// values for the OCR1A register; the lower the value, the harder it is
unsigned int speed[MAX_LEVEL];

uint8_t analog_pin = HORIZONTAL;
uint8_t direction = UP; // DEFAULT

void init_converter() {
  ADMUX = 1;
  /* AVCC with external capacitor at AREF pin */
  ADMUX |= (1 << REFS0);

  ADCSRA = 0;
  /* set prescaler at 128 */
  ADCSRA |= (7 << ADPS0);

  // Enable auto-trigger
 // ADCSRA |= (1 << ADATE);
  // Enable Intrerupt
  //ADCSRA |= (1 << ADIE);

  ADCSRA |= (1 << ADEN);
}

void init_timer() {
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = speed[current_level];
  TCCR1B |= (1 << WGM12);   // CTC mode
  TCCR1B |= (1 << CS12);    // 256 prescaler
  TIMSK1 |= (1 << OCIE1A);
}

void init_button_intr() {
  pinMode(2, INPUT_PULLUP);
  EICRA = 0;
  EIMSK = 0;
  
  // falling edge
  EICRA |= 1 << ISC01;

  // activate INT0
  EIMSK |= 1 << INT0;
}

/*
 * checks the input from the joystick
 * return 0 if no input is detected;
 * input is detected if the result is below 200 or above 800
 * input must be read only once between two updates on the RGB LED
*/
bool check_input_detected() {
  uint32_t result = analogRead(analog_pin);

  if (was_read)
    return 0;

  if (result > JOYSTICK_LOWER_THRESHOLD && result < JOYSTICK_UPPER_THRESHOLD)
    return 0;

  // update the direction
  uint8_t prev_direction = direction;
  if (analog_pin == VERTICAL) {
     // A0
     if (result < JOYSTICK_LOWER_THRESHOLD) {
      direction = UP;
     } else if (result > JOYSTICK_UPPER_THRESHOLD) {
      direction = DOWN;
     }
  } else {
      // A1 - HORIZONTAL
      if (result < JOYSTICK_LOWER_THRESHOLD) {
      direction = LEFT;
     } else if (result > JOYSTICK_UPPER_THRESHOLD) {
      direction = RIGHT;
     }
  }

  // change the analog pin if the direction changed
  if (prev_direction != direction) {
    if (analog_pin == HORIZONTAL)
      analog_pin = VERTICAL;
    else
      analog_pin = HORIZONTAL;
  }

  was_read = 1;
  return 1;
}

void start_game() {
  game_state = RUNNING;
  score = 0;
  snake_dim = 1;
  snake_body[0] = random(0, NUMPIXELS);
  prey = random(0, NUMPIXELS);
  direction = UP;
  analog_pin = HORIZONTAL;
}

void compute_next_position() {
  // update the position of the head given the direction
  uint8_t tmp = snake_body[0];
  uint8_t tail = snake_body[snake_dim - 1];

  switch(direction) {
    case UP:
      if (snake_body[0] / NR_ROWS == NR_ROWS - 1) // upper margin
        snake_body[0] %= NR_ROWS; // next position will be the on the lower margin
      else
        snake_body[0] += NR_ROWS;
      break;

    case DOWN:
      if (snake_body[0] / NR_ROWS == 0) // lower margin
        snake_body[0] += NR_ROWS * (NR_ROWS - 1); // next position will be the on the upper margin
      else
        snake_body[0] -= NR_ROWS;
      break;

    case LEFT:
      if (snake_body[0] % NR_ROWS == NR_ROWS - 1) // left margin
        snake_body[0] -= NR_ROWS - 1; // next position will be the on the right margin
      else
        snake_body[0]++;
      break;

    case RIGHT:
      if (snake_body[0] % NR_ROWS == 0) // right margin
        snake_body[0] += NR_ROWS - 1; // next position will be the on the left margin
      else
        snake_body[0]--;
      break;
  }

  // update the vector
  for (int i = 1; i < snake_dim; i++) {
    uint8_t tmp1 = snake_body[i];
    snake_body[i] = tmp;
    tmp = tmp1;
  }

  // if the snake catches the prey, it grows in size
  if (snake_body[0] == prey) {
    snake_body[snake_dim++] = tail;
    score++;

    // also spawn another prey;
    prey = random(0, NUMPIXELS);
  }

  // check if the snake runs into itself
  for (int i = 1; i < snake_dim; i++) {
    if (snake_body[0] == snake_body[i])
      game_state = GAME_OVER;
      leds_on = 1;
  }
  
}

void print_score() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Score is: ");
  lcd.print(score);

  // printing the level
  lcd.setCursor(0, 1);
  if (current_level == 0)
    lcd.print("EASY");
  else if (current_level == 1)
    lcd.print("MEDIUM");
  else
    lcd.print("HARD");
}

void print_game_over() {
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Game over!");

  lcd.setCursor(0, 1);
  lcd.print("Final score: ");
  lcd.print(score);
  
}

// turn on the LEDs in RUNNING mode
void display_running() {
  // next position is always computed if an input from the joystick is detected
  // but if no such input is detected, then it must be computed here
  if (!was_read){
    compute_next_position();
  }

  //Serial.print("yes\n");
  
 // turn all LEDs off
 pixels.clear();
 bool snake_over_prey = 0;
 
 // turn on SNAKE LEDs
 for (int i = 1; i < snake_dim; i++) {
    pixels.setPixelColor(snake_body[i], pixels.Color(255, 0, 0));

    if (snake_body[i] == prey)
      snake_over_prey = 1;
 }

 // turn on the LED of the snake's head
 pixels.setPixelColor(snake_body[0], pixels.Color(230, 200, 9));

 // turn on PREY LED only if the prey is not below the snake
 // the prey might spawn on the snake's body
 if (!snake_over_prey)
    pixels.setPixelColor(prey, pixels.Color(0, 255, 0));
 
 pixels.show();
 was_read = 0;

 // the speed of the game must be updated
 OCR1A = speed[current_level];

 print_score();
}

void display_game_over() {
  if (leds_on) {
    // turn on the leds
    pixels.clear();
    for (int i = 1; i < snake_dim; i++) {
      pixels.setPixelColor(snake_body[i], pixels.Color(255, 0, 0));
    }
    
    pixels.setPixelColor(snake_body[0], pixels.Color(230, 200, 9));
    pixels.show();
  } else {
    // turn leds off
    pixels.clear();
    pixels.show();
  }

  Serial.println(leds_on);

  leds_on = !leds_on;
  print_game_over();
}

#define MIN_BUTTON_TIME 250
unsigned long last_button_press = 0;

ISR(INT0_vect)
{ 
  if (millis() - last_button_press < MIN_BUTTON_TIME)
    return;

  last_button_press = millis();

  if (game_state == RUNNING) {
    // increment the current level and reset it if necessary
    if (++current_level == MAX_LEVEL) {
      current_level = 0;
    } 
  } else {
    // when pressing the button, the game resets
    start_game();
    }
}

ISR(TIMER1_COMPA_vect) {
  if (game_state == RUNNING)
    display_running();
  else if (game_state == GAME_OVER)
    display_game_over();
}

void setup() {
  Serial.begin(9600);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(1,0);
  lcd.print("Welcome");

  delay(1000);

  lcd.clear();

  // init the speed vector
  speed[0] = 46500;
  speed[1] = 31250;
  speed[2] = 20000;

  cli();
  
  init_button_intr();
  //init_converter();
  init_timer();

  sei();
  pixels.begin();

  // setting the brightness to a minimum so that it won't burn your eyes
  pixels.setBrightness(1);
  
  randomSeed(millis());
  start_game();
}

void loop() {
  if (game_state == RUNNING) {
    if (check_input_detected()) {
      compute_next_position();
    }
  }
}
