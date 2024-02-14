#include <SoftwareSerial.h>
#include <MFRC522.h>
#include <SPI.h>
#include <Keypad.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#define RST_PIN 9
#define SS_PIN 53

Servo myServo;

String keydata = "";
bool collectingData = false; // Flag to indicate whether to collect data
int is_add_data = 0;
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columnsq  
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {37, 35, 33,31};
byte colPins[COLS] = {45, 43, 41, 39};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

SoftwareSerial Arduino_SoftSerial(18,19); //RX TX
String uid ="";
MFRC522 mfrc522(SS_PIN, RST_PIN); // Create MFRC522 instance
String Data;

// Set the LCD address (usually 0x27 for a 16x2 display)
int lcdAddress = 0x27;

// Set the LCD number of columns and rows
int lcdColumns = 16;
int lcdRows = 2;

LiquidCrystal_I2C lcd(lcdAddress, lcdColumns, lcdRows);

String getUID() {
  String uid = "";
  // Concatenate the UID bytes into a string
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : ""));
    uid.concat(String(mfrc522.uid.uidByte[i], HEX));
  }
  return uid;
}
String getOTP(){
  String otp = "";
  while(Arduino_SoftSerial.available()){
    otp = Arduino_SoftSerial.read();
  }
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115000);
  Arduino_SoftSerial.begin(9600);
  SPI.begin();        // Init SPI bus
  mfrc522.PCD_Init();   // Initiate MFRC522
  Serial.println("Reading RFID");

  myServo.attach(9);
 // Initialize the I2C communication
  Wire.begin();
  
  // Initialize the LCD
  lcd.init();
  
  // Turn on the backlight (if available)
  lcd.backlight();
  
  // Print a message to the LCD
  lcd.setCursor(1, 0);
  lcd.print("Hello, World!");
}

void loop() {
    // put your main code here, to run repeatedly:
  
  // Reset the loop if no new card present on the sensor/reader. This saves the entire process when idle.
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
      Serial.println("RFID detected!");
      // Store UID in a variable
      uid = getUID();
      Serial.print("UID: ");
      Serial.println(uid);
      // Stop reading the card
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      delay(1000); // Delay to prevent reading the same card multiple times

    }
    char key = keypad.getKey();
    if (key != NO_KEY) {
      if (key=='#'){
        collectingData = 1;
      }
      if (key=='*'){
        collectingData = 0;
        Serial.println("Stop collecting data");
        LCD.setcursor(0,0);
        LCD.write("Door Open");
      }
      if(collectingData){
        Serial.print("Collecting data");
        keydata += key;
        Serial.println("Keydata:" + keydata);
        LCD.setcursor(0,0);
        LCD.write(keydata);
      }
  }

    if (uid=="7905b0a2" || uid =="d91ba299" || uid == "d7c57b62"|| uid == "777dbc60" || otp==keydata){
      LCD.setcursor(0,0);
      LCD.write("Door Open");
      Serial.print("Door Open");
      myServo.write(0);
      delay(1000); // Wait for 1 second

  // Rotate the servo to 90 degrees
      myServo.write(90);
      delay(1000); // Wait for 1 second
      Serial.print("Door Close")
      keydata = "";
      uid ="";
    }
    else{
      Serial.print("Door Close");
        }
    Arduino_SoftSerial.print(uid);
    delay(500);
  }
   