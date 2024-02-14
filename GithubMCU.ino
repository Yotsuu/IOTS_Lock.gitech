// Include necessary libraries
#include <ArduinoJson.h>
#include <Servo.h> 
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiclientSecure.h>
#include <UniversalTelegramBot.h>
#include <Adafruit_Fingerprint.h>
#include <time.h>
#include "secrets.h"
#include "DHT.h"
#include<SoftwareSerial.h> //Included SoftwareSerial Library
//Started SoftwareSerial at RX and TX pin of ESP8266/NodeMCU
SoftwareSerial s(3,1);
#define TIME_ZONE 3

WiFiClientSecure wifiClient;
BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);
PubSubClient client(wifiClient);

time_t now;
time_t nowish = 1510592825;
unsigned long lastMillis = 0;
unsigned long previousMillis = 0;
const long interval = 5000;

#define AWS_IOT_PUBLISH_TOPIC   "esp8266/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp8266/sub"
// Structure to hold user data
struct UserData {
  char telegramHandle[32];
  char password[32];
  uint8_t fingerprintID;
  bool hasEnrolledFingerprint;
};

#if (defined(_AVR) || defined(ESP8266)) && !defined(AVR_ATmega2560_)
SoftwareSerial mySerial(13, 15);
#else
#define mySerial Serial1
#endif

// Declarations of network credentials/Replace with your network credentials
const char* ssid = "20192";
const char* password = "jpnh7007";

// Initialize Telegram BOT  
#define BOTtoken "6711010158:AAHXYqoibmzM-AExU1zSEWLeAkb4FSIye_s"
#define CHAT_ID "your_chat_id"

// Provide certification
X509List teleCert(TELEGRAM_CERTIFICATE_ROOT);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

String currentTelegramHandle = "";

Servo servo1;                          // create servo object to control a servo
                            // variable to store the servo position

UniversalTelegramBot bot(BOTtoken, wifiClient);

// Checks for new messages every 1 second.
int botRequestDelay = 1000;
unsigned long lastTimeBotRan;
int otp;

const int ledPin = 2;

// EEPROM address for storing the number of registered users
int eepromUserCountAddress = 0;
int eepromUserStartAddress = 1;  // Starting address for user data

// Maximum number of users
const int maxUsers = 4;

enum FingerprintEnrollmentState {
  ENROLL_IDLE,
  ENROLL_REQUESTED,
  ENROLL_IN_PROGRESS,
};

FingerprintEnrollmentState enrollmentState = ENROLL_IDLE;

// Variable to store the last login time
const int logoutTimeout = 1800000;  // Timeout for automatic logout (milliseconds)
unsigned long lastLogoutTime = 0;
unsigned long lastLoginTime = 0;

UserData registeredUsers[maxUsers];

// Variable to store the index of the logged-in user
int loggedInUserIndex = -1;
int usertime = 0;

/* Function prototypes */
void handleNewMessages(int numNewMessages);
void registerUser(String telegramHandle);
void saveUserData(int userIndex);
void loadUserData(int userIndex);
bool isUserLoggedIn(String telegramHandle, int& userIndex);
void login(String telegramHandle);
bool loginUser(String telegramHandle);
void sendForgotPassword(String telegramHandle);
void generateAndSendOTP(String telegramHandle);
void resetPassword(String telegramHandle);
void processPasswordInput(String telegramHandle, String password);
void showHelp(String telegramHandle);
void deleteAccount(String telegramHandle);
void sendViewMessage(String telegramHandle);
void resetEEPROM();
bool enrollFingerprintCommand(String telegramHandle);
int readnumber();
bool enrollFingerprintInternal(uint8_t fingerprintID, String telegramHandle);
int findUserByTelegramHandle(String telegramHandle);
void connectAWS();
void connectWifiTele();
void connectWifiAWS();
void handleLogout(String telegramHandle);
void publishMessage(String telegramHandle, String message);
bool verifyFingerprint(String telegramHandle, uint8_t expectedFingerprintID);
bool verifyFingerprintdoor(String telegramHandle, uint8_t expectedFingerprintID);

void setup() {
  Serial.begin(115200);

  //Serial communications Begin at 9600 Baud 
  s.begin(9600);

  // Configure time and set trust anchors for telegram
  configTime(0, 0, "pool.ntp.org");
  wifiClient.setTrustAnchors(&teleCert);

  // Set up LED pin
  pinMode(ledPin, OUTPUT);

  // Initialize EEPROM
  EEPROM.begin(maxUsers * sizeof(UserData) + 1);

  // Check if user count is initialized in EEPROM
  int userCount;
  EEPROM.get(eepromUserCountAddress, userCount);

  if (userCount < 0 || userCount > maxUsers) {
    // If not initialized or out of bounds, initialize to 0
    userCount = 0;
    EEPROM.put(eepromUserCountAddress, userCount);
    EEPROM.commit();
  }

  // Reset the user count to 0 before loading user data
  userCount = 0;

  servo1.attach (0);      // attaches the servo on pin 0 to the servo object

  // Load user data from EEPROM and update user count
  for (int i = 0; i < maxUsers; i++) {
    loadUserData(i);
    if (registeredUsers[i].telegramHandle[0] != '\0') {
      userCount++;
    }
  }

  // Update the user count in EEPROM
  EEPROM.put(eepromUserCountAddress, userCount);
  EEPROM.commit();

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  // Wait for Wi-Fi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi..");
    Serial.println(WiFi.localIP());
  }

  finger.begin(57600);
  for (int i =0; i > 10; i++) {
    if (finger.verifyPassword()) {
      Serial.println("Found fingerprint sensor!");
      break;
    } else {
      Serial.println("Did not find fingerprint sensor :(");
      delay(1000);
      break;
    }
  }
  connectAWS();
}

void loop() {
  // Check if it's time to request new messages from the Telegram API
  connectWifiTele();
  if (millis() > lastTimeBotRan + botRequestDelay) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    handleNewMessages(numNewMessages);
    lastTimeBotRan = millis();
  }

  // Handle fingerprint enrollment if it's in progress
  if (enrollmentState == ENROLL_REQUESTED) {
    connectWifiTele();
    // Call the function to enroll fingerprint
    enrollFingerprintCommand(currentTelegramHandle);
    enrollmentState = ENROLL_IN_PROGRESS;
  }

  // Check for inactivity and log out if needed
  if (currentTelegramHandle != "" && (millis() - lastLoginTime) > logoutTimeout) {
    logout();
  }
}

// Function to handle new messages from the Telegram API
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String telegramHandle = String(bot.messages[i].chat_id);
    String receivedMessage = bot.messages[i].text;

    // Check if the user is authorized
    if (telegramHandle.isEmpty()) {
      bot.sendMessage(bot.messages[i].chat_id, "Unauthorized user", "");
      continue;
    }

    String text = bot.messages[i].text;

    // Process different commands
    if (text == "/start") {
      login(telegramHandle);
    }
    if (text == "/Login") {
      login(telegramHandle);
    }
    if (text == "/logout") {
      handleLogout(telegramHandle);
    }
    if (text == "/Register") {
      registerUser(telegramHandle);
    }
    if (text == "/forgot") {
      sendForgotPassword(telegramHandle);
    }
    if (text == "/gotp") {
      generateAndSendOTP(telegramHandle);
    }
    if (text == "/reset_password") {
      resetPassword(telegramHandle);
    }
    if (text == "/help") {
      showHelp(telegramHandle);
    }
    // New condition to handle "/view" command
    if (text == "/view") {
      sendViewMessage(telegramHandle);
    }
    // Command to delete account
    if (text == "/delete_account") {
      deleteAccount(telegramHandle);
    }
    if (text == "/fingerprintDoor") {
      fingerprintDoor(telegramHandle);
    }
    if (text == "/reset_eeprom") {
      resetEEPROM();
      bot.sendMessage(telegramHandle, "EEPROM data and user count reset.", "");
      return;
    }
    // Commands to process password input
    if (text.startsWith("/Login_password ")) {
      processPasswordInput(telegramHandle, text.substring(16));
    }
    // New condition to handle "/enroll" command
    if (text == "/enroll") {
      // Check if the user is logged in
      if (isUserLoggedIn(telegramHandle, loggedInUserIndex)) {
        // Save the current Telegram handle for enrollment
        currentTelegramHandle = telegramHandle;
        enrollmentState = ENROLL_REQUESTED;
      } else {
        bot.sendMessage(telegramHandle, "You must be logged in to enroll fingerprints.", "");
      }
    }

    if (text == "/fingerprint_login") {
      fingerprintLogin(telegramHandle);
    }
    // Print every line that the user sends to the Arduino's serial monitor
    Serial.println("User Message: " + receivedMessage);
    Serial.println("User handle: " + telegramHandle);
    connectWifiAWS();
    if (!client.connected())
    {
      connectAWS();
      client.loop();
      if (millis() - lastMillis > 5000)
      {
        lastMillis = millis();
        publishMessage(telegramHandle, receivedMessage);
      }
    }
    else
    {
      client.loop();
      if (millis() - lastMillis > 5000)
      {
        lastMillis = millis();
        publishMessage(telegramHandle, receivedMessage);
      }
    }
  }
}

// Function to handle user registration
void registerUser(String telegramHandle) {
  // Check if the user is already registered
  if (findUserByTelegramHandle(telegramHandle) != -1) {
    bot.sendMessage(telegramHandle, "You are already registered. Use /Login to log in.", "");
    return;
  }

  // Check if the maximum number of users is reached
  int userCount;
  EEPROM.get(eepromUserCountAddress, userCount);
  if (userCount >= maxUsers) {
    bot.sendMessage(telegramHandle, "Cannot register more users. Maximum limit reached.", "");
    return;
  }

  // Prompt user to set a password
  bot.sendMessage(telegramHandle, "Set a password for your account:", "");

  while (1) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0 && bot.messages[0].text != "/Register") {
      String password = bot.messages[0].text;

      // Check if the password meets the minimum requirements
      if (password.length() >= 8) {
        // Save user data
        strcpy(registeredUsers[userCount].telegramHandle, telegramHandle.c_str());
        strcpy(registeredUsers[userCount].password, password.c_str());
        registeredUsers[userCount].fingerprintID = 1;  // Initialize fingerprint ID to 1

        // Increment user count and save to EEPROM
        userCount++;
        EEPROM.put(eepromUserCountAddress, userCount);

        // Save user data to EEPROM
        saveUserData(userCount - 1);

        bot.sendMessage(telegramHandle, "Registration successful!", "");
        registeredUsers[userCount].hasEnrolledFingerprint = false;
        return;
      } else {
        bot.sendMessage(telegramHandle, "Password must be at least 8 characters long. Please try again:", "");
      }
    }
    delay(500);
  }
}

// Function to save user data to EEPROM
void saveUserData(int userIndex) {
  int address = eepromUserStartAddress + userIndex * sizeof(UserData);
  EEPROM.put(address, registeredUsers[userIndex]);
  EEPROM.commit();
}

// Function to load user data from EEPROM
void loadUserData(int userIndex) {
  int address = eepromUserStartAddress + userIndex * sizeof(UserData);
  EEPROM.get(address, registeredUsers[userIndex]);
}

// Function to check if a user is logged in and get their index
bool isUserLoggedIn(String telegramHandle, int& userIndex) {
  for (int i = 0; i < maxUsers; i++) {
    if (strcmp(telegramHandle.c_str(), registeredUsers[i].telegramHandle) == 0) {
      // User found, check if logged in
      if (i == loggedInUserIndex) {
        userIndex = i;
        return true;
      } else {
        // User found but not logged in
        userIndex = i;
        return false;
      }
    }
  }
  // User not found
  userIndex = -1;
  return false;
}


void logout() {
  bot.sendMessage(currentTelegramHandle, "You have been logged out", "");
  currentTelegramHandle = "";
  loggedInUserIndex = -1;
}

// Function to handle login
void login(String telegramHandle) {
  // Check if the user is already logged in
  if (isUserLoggedIn(telegramHandle, loggedInUserIndex)) {
    bot.sendMessage(telegramHandle, "You are already logged in.", "");
    lastLoginTime = millis();  // Reset the timeout on new login
    return;
  }

  // Check if the user is registered
  int userIndex = findUserByTelegramHandle(telegramHandle);
  if (registeredUsers[userIndex].hasEnrolledFingerprint)
    bot.sendMessage(telegramHandle, "You can also use your fingerprint to log in. Use /fingerprint_login.", "");
     if (userIndex != -1) {
      // User found, prompt for password
      bot.sendMessage(telegramHandle, "Enter your password to log in:", "");

      while (1) {
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        if (numNewMessages > 0 && bot.messages[0].text != "/start") {
          String enteredPassword = bot.messages[0].text;

          // Check if the entered password matches the stored password
          if (strcmp(enteredPassword.c_str(), registeredUsers[userIndex].password) == 0) {
            // Password matched, user is logged in
            loggedInUserIndex = userIndex;
            currentTelegramHandle = telegramHandle;
            lastLoginTime = millis();
            bot.sendMessage(telegramHandle, "Login successful!", "");
            return;
          } else {
            // Password did not match
            bot.sendMessage(telegramHandle, "Incorrect password. Please try again.", "");
            return;
          }
        }
        delay(500);
      }
    }
  else {
    // User not found
    bot.sendMessage(telegramHandle, "User not found. Use /Register to create an account.", "");
  }
}

void fingerprintLogin(String telegramHandle) {
  int userIndex;
  if (isUserLoggedIn(telegramHandle, userIndex)) {
    bot.sendMessage(telegramHandle, "You are already logged in.", "");
    return;
  }

  if (!registeredUsers[userIndex].hasEnrolledFingerprint) {
    bot.sendMessage(telegramHandle, "You haven't enrolled a fingerprint. Use /enroll to enroll one.", "");
    return;
  }

  // Retrieve expected fingerprint ID
  uint8_t expectedFingerprintID = registeredUsers[userIndex].fingerprintID;

  // Perform fingerprint verification
  if (verifyFingerprint(telegramHandle, expectedFingerprintID)) {
    // Fingerprint matched, user is logged in
    loggedInUserIndex = userIndex;
    currentTelegramHandle = telegramHandle;
    lastLoginTime = millis();
    bot.sendMessage(telegramHandle, "Login successful!", "");
  } else {
    // Fingerprint did not match
    bot.sendMessage(telegramHandle, "Fingerprint verification failed. Please try again.", "");
  }
}

void fingerprintDoor(String telegramHandle) {
  int userIndex;
  if (!registeredUsers[userIndex].hasEnrolledFingerprint) {
    bot.sendMessage(telegramHandle, "You haven't enrolled a fingerprint. Use /enroll to enroll one.", "");
    return;
  }

  // Retrieve expected fingerprint ID
  uint8_t expectedFingerprintID = registeredUsers[userIndex].fingerprintID;

  // Perform fingerprint verification
  if (verifyFingerprintdoor(telegramHandle, expectedFingerprintID)) {
    // Fingerprint matched, user is logged in
    //code to open 
    bot.sendMessage(telegramHandle, "Door opening", "");
    for (posn = 0; posn < 180; posn += 1)// goes from 0 degrees to 180 degrees
    {// in steps of 1 degree
    servo1.write (posn);// tell servo to go to position in variable 'pos'
    delay (10);// waits 10ms for the servo to reach the position
    }
    bot.sendMessage(telegramHandle, "Door closing", "");
    delay (5000);
  for (posn = 180; posn>=1; posn-=1)// goes from 180 degrees to 0 degrees                                                                                 // in steps of 1 degree
    {                               
    servo1.write (posn);// tell servo to go to position in variable 'pos'
    delay (10);// waits 10ms for the servo to reach the position  }
    } 
  }
  else {
    // Fingerprint did not match
    bot.sendMessage(telegramHandle, "Fingerprint verification failed. Please try again.", "");
  }
}



// Function to verify fingerprint against the stored fingerprint ID
bool verifyFingerprint(String telegramHandle, uint8_t expectedFingerprintID) {
  int p = -1;
  bot.sendMessage(telegramHandle, "Place your finger on the scanner for verification.", "");
  // Wait for a valid fingerprint image
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return false;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return false;
      default:
        Serial.println("Unknown error");
        return false;
    }
  }

  // Convert the fingerprint image to a template
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion error");
    return false;
  }

  // Search for the fingerprint in the database
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    // Fingerprint matched
    int id = finger.fingerID;
    int confidence = finger.confidence;
    Serial.println("Found fingerprint with ID " + String(id) + " and confidence " + String(confidence));

    // Check if the found fingerprint ID matches the expected ID
    if (id == expectedFingerprintID) {
      Serial.println("Fingerprint verified successfully");
      return true;
    } else {
      Serial.println("Fingerprint ID mismatch");
      return false;
    }
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error during search");
    return false;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found");
    return false;
  } else {
    Serial.println("Unknown error during search");
    return false;
  }
}
bool verifyFingerprintdoor(String telegramHandle, uint8_t expectedFingerprintID) {
  int p = -1;
  bot.sendMessage(telegramHandle, "Place your finger on the scanner for verification.", "");
  // Wait for a valid fingerprint image
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        return false;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        return false;
      default:
        Serial.println("Unknown error");
        return false;
    }
  }

  // Convert the fingerprint image to a template
  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Image conversion error");
    return false;
  }

  // Search for the fingerprint in the database
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    // Fingerprint matched
    int id = finger.fingerID;
    int confidence = finger.confidence;
    Serial.println("Found fingerprint with ID " + String(id) + " and confidence " + String(confidence));
    return true;

    // Check if the found fingerprint ID matches the expected ID
    if (id == expectedFingerprintID) {
      Serial.println("Fingerprint verified successfully");
      return true;
    } else {
      Serial.println("Fingerprint ID mismatch");
      return false;
    }
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error during search");
    return false;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint not found");
    return false;
  } else {
    Serial.println("Unknown error during search");
    return false;
  }
}
// Function to find a user by their Telegram handle
int findUserByTelegramHandle(String telegramHandle) {
  for (int i = 0; i < maxUsers; i++) {
    if (strcmp(telegramHandle.c_str(), registeredUsers[i].telegramHandle) == 0) {
      // User found
      return i;
    }
  }
  // User not found
  return -1;
}

void handleLogout(String telegramHandle) {
  // Check if the user is logged in
  if (isUserLoggedIn(telegramHandle, loggedInUserIndex)) {
    // Perform logout
    logout();
  } else {
    // User not logged in
    bot.sendMessage(telegramHandle, "You are not logged in.", "");
  }
}

// Function to handle sending forgot password instructions
void sendForgotPassword(String telegramHandle) {
  int userIndex = findUserByTelegramHandle(telegramHandle);

  if (userIndex != -1) {
    // User found, generate and send OTP
    generateAndSendOTP(telegramHandle);
    bot.sendMessage(telegramHandle, "An OTP has been sent to your registered email address. Use /reset_password to reset your password.", "");
  } else {
    // User not found
    bot.sendMessage(telegramHandle, "User not found. Use /Register to create an account.", "");
  }
}

// Function to generate and send OTP to the user's email address (simulated)
void generateAndSendOTP(String telegramHandle) {
  Generate a random 6-digit OTP
  otp = random(100000, 999999);

  //send otp via serial comms to arduino
  S.write(otp);
  bot.sendMessage(telegramHandle, "Your otp is " + String(otp), "");
}

// Function to reset the user's password after verifying OTP
void resetPassword(String telegramHandle) {
  // Check if the OTP matches the generated OTP
  bot.sendMessage(telegramHandle, "Enter the OTP sent to your email to reset your password:", "");

  while (1) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0 && bot.messages[0].text != "/reset_password") {
      String enteredOTP = bot.messages[0].text;

      if (enteredOTP.toInt() == otp) {
        // OTP matched, prompt for a new password
        bot.sendMessage(telegramHandle, "Enter your new password:", "");

        while (1) {
          int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
          if (numNewMessages > 0 && bot.messages[0].text != "/reset_password") {
            String newPassword = bot.messages[0].text;

            // Check if the new password meets the minimum requirements
            if (newPassword.length() >= 4) {
              // Update the user's password and save to EEPROM
              int userIndex = findUserByTelegramHandle(telegramHandle);
              strcpy(registeredUsers[userIndex].password, newPassword.c_str());
              saveUserData(userIndex);

              bot.sendMessage(telegramHandle, "Password reset successful!", "");
              return;
            } else {
              bot.sendMessage(telegramHandle, "Password must be at least 4 characters long. Please try again:", "");
            }
          }
          delay(500);
        }
      } else {
        bot.sendMessage(telegramHandle, "Incorrect OTP. Please try again.", "");
        return;
      }
    }
    delay(500);
  }
}

// Function to process password input for login
void processPasswordInput(String telegramHandle, String password) {
  int userIndex = findUserByTelegramHandle(telegramHandle);
  if (userIndex != -1) {
    // User found, check if the entered password matches the stored password
    if (strcmp(password.c_str(), registeredUsers[userIndex].password) == 0) {
      // Password matched, user is logged in
      loggedInUserIndex = userIndex;
      currentTelegramHandle = telegramHandle;
      lastLoginTime = millis();
      bot.sendMessage(telegramHandle, "Login successful!", "");
    } else {
      // Password did not match
      bot.sendMessage(telegramHandle, "Incorrect password. Please try again.", "");
    }
  } else {
    // User not found
    bot.sendMessage(telegramHandle, "User not found. Use /Register to create an account.", "");
  }
}

// Function to display help information
void showHelp(String telegramHandle) {
  bot.sendMessage(telegramHandle, "Available commands:\n"
                                  "/start - Start the bot\n"
                                  "/Login - Log in to your account\n"
                                  "/Register - Register a new account\n"
                                  "/forgot - Reset your password\n"
                                  "/gotp - Generate and send OTP\n"
                                  "/reset_password - Reset your password\n"
                                  "/help - Show help information\n"
                                  "/view - View account information\n"
                                  "/delete_account - Delete your account\n"
                                  "/enroll - Enroll fingerprint\n"
                                  "/logout - Log out\n"
                                  "/fingerprintDoor - use fingerprint to open door",
                  "");
}

// Function to delete user account
void deleteAccount(String telegramHandle) {
  int userIndex = findUserByTelegramHandle(telegramHandle);

  if (userIndex != -1) {
    // User found, delete account
    for (int i = userIndex; i < maxUsers - 1; i++) {
      // Shift user data to overwrite the current user's data
      registeredUsers[i] = registeredUsers[i + 1];
      // Save the updated user data to EEPROM
      saveUserData(i);
    }

    // Decrement user count and save to EEPROM
    int userCount;
    EEPROM.get(eepromUserCountAddress, userCount);
    userCount--;
    EEPROM.put(eepromUserCountAddress, userCount);

    // Clear the data of the last user in EEPROM
    int address = eepromUserStartAddress + userCount * sizeof(UserData);
    for (int i = 0; i < sizeof(UserData); i++) {
      EEPROM.write(address + i, 0);
    }
    EEPROM.commit();

    // Reset logged-in user index if the deleted user was logged in
    if (userIndex == loggedInUserIndex) {
      loggedInUserIndex = -1;
    }

    bot.sendMessage(telegramHandle, "Account deleted successfully.", "");
  } else {
    // User not found
    bot.sendMessage(telegramHandle, "User not found. Use /Register to create an account.", "");
  }
}

// Function to send a view message with account information
void sendViewMessage(String telegramHandle) {
  // Fetch total registered users count from EEPROM

  int userIndex = findUserByTelegramHandle(telegramHandle);

  if (userIndex != -1) {
    // User found, send account information
    String viewMessage = "Account Information:\n"
                         "Telegram Handle: "
                         + String(registeredUsers[userIndex].telegramHandle) + "\n"
                                                                               "Fingerprint ID: "
                         + String(registeredUsers[userIndex].fingerprintID);
    // Fetch total registered users count from EEPROM
    int totalUsers;
    EEPROM.get(eepromUserCountAddress, totalUsers);
    viewMessage += "\nTotal Registered Users: " + String(totalUsers);
  } else {
    // User not found
    bot.sendMessage(telegramHandle, "User not found. Use Register to create an account.", "");
  }
}

// Function to reset EEPROM data and user count
void resetEEPROM() {
  for (int i = 0; i < maxUsers; i++) {
    // Clear user data in EEPROM
    int address = eepromUserStartAddress + i * sizeof(UserData);
    for (int j = 0; j < sizeof(UserData); j++) {
      EEPROM.write(address + j, 0);
    }
  }

  // Reset user count to 0
  int userCount = 0;
  EEPROM.put(eepromUserCountAddress, userCount);
  finger.emptyDatabase();
  // Commit changes to EEPROM
  EEPROM.commit();
}

// Function to handle the "/enroll" command
bool enrollFingerprintCommand(String telegramHandle) {
  int userIndex;
  if (!isUserLoggedIn(telegramHandle, userIndex)) {
    bot.sendMessage(telegramHandle, "Enrollment requires login. Use /Login and try again.", "");
    return false;
  }

  // Use the fingerprintID as the fingerprint ID
  uint8_t fingerprintID = registeredUsers[userIndex].fingerprintID;

  bot.sendMessage(telegramHandle, "Ready to enroll a fingerprint!", "");
  delay(500);

  if (enrollFingerprintInternal(fingerprintID, telegramHandle)) {
    bot.sendMessage(telegramHandle, "Fingerprint enrollment successful.", "");
    registeredUsers[userIndex].hasEnrolledFingerprint = true;
  } else {
    bot.sendMessage(telegramHandle, "Fingerprint enrollment failed. Please try again.", "");
    return false;
  }
  return true;
}


// Internal function to handle fingerprint enrollment
bool enrollFingerprintInternal(uint8_t fingerprintID, String telegramHandle) {
  int p = -1;

  // Wait for a valid fingerprint image
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.println(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
  }

  // Convert the fingerprint image to a template
  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return false;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return false;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return false;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return false;
    default:
      Serial.println("Unknown error");
      return false;
  }

  Serial.println("Remove finger");
  bot.sendMessage(telegramHandle, "Remove finger\n", "");
  delay(2000);

  // Wait for the user to remove their finger
  p = 0;
  while (p != FINGERPRINT_NOFINGER) {
    p = finger.getImage();
  }

  p = -1;
  Serial.println("Place same finger again");
  bot.sendMessage(telegramHandle, "Place same finger again\n", "");

  // Wait for a valid fingerprint image again
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        break;
      case FINGERPRINT_NOFINGER:
        Serial.print(".");
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error");
        break;
      default:
        Serial.println("Unknown error");
        break;
    }
  }

  // Convert the second fingerprint image to a template
  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return false;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error");
      return false;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return false;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return false;
    default:
      Serial.println("Unknown error");
      return false;
  }

  // Create a fingerprint model
  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }

  Serial.print("ID ");
  Serial.println(fingerprintID);

  // Store the fingerprint model with the given ID
  p = finger.storeModel(fingerprintID);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return false;
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    return false;
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    return false;
  } else {
    Serial.println("Unknown error");
    return false;
  }

  return true;
}

// Function to read a number from the user input
int readnumber() {
  while (1) {
    int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    if (numNewMessages > 0 && bot.messages[0].text != "/enroll") {
      return bot.messages[0].text.toInt();
    }
    delay(500);
  }
}

void NTPConnect(void)
{
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void connectAWS()
{ 
  NTPConnect();
 
  wifiClient.setTrustAnchors(&cert);
  wifiClient.setClientRSACert(&client_crt, &key);
 
  client.setServer(MQTT_HOST, 8883);
  client.setCallback(messageReceived);
 
  Serial.println("Connecting to AWS IOT");
 
  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(1000);
  }
 
  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
 
  Serial.println("AWS IoT Connected!");
}

void publishMessage(String telegramHandle, String message)
{
  connectWifiAWS();
  StaticJsonDocument<200> doc;
  doc["time"] = millis();
  doc["TelegramHandle"] = telegramHandle;
  doc["Message"] = message;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
 
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}

void messageReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void connectWifiTele(){
  wifiClient.setTrustAnchors(&teleCert);
}

void connectWifiAWS(){
  wifiClient.setTrustAnchors(&cert);
  NTPConnect();
 
  wifiClient.setClientRSACert(&client_crt, &key);
 
  client.setServer(MQTT_HOST, 8883);
  client.setCallback(messageReceived);
 
  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(1000);
  }
 
  if (!client.connected()) {
    return;
  }
  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
}