#include <Arduino.h>
#include <sys/time.h>

// =====================================
// MP3 MODULE CONFIGURATION
// =====================================

#define MP3_RX 18 // A1
#define MP3_TX 17 // A0

#define MP3_SERIAL Serial2

// =====================================
// TIMER VARIABLES
// =====================================

long timerLength = 0;
int64_t startTime = 0;
long previousTime = -1;

bool timerRunning = false;
bool audioPlaying = false;

// =====================================
// VOLUME VARIABLES
// =====================================

int volume = 20;

const int MIN_VOLUME = 0;
const int MAX_VOLUME = 30;
const int VOLUME_STEP = 5;

// =====================================
// AUDIO TRACK VARIABLES
// =====================================

int currentTrack = 1;

// =====================================
// RTC TIME FUNCTION
// =====================================

int64_t getRTCtime() {

  struct timeval currentTime;

  gettimeofday(&currentTime, NULL);

  return ((int64_t)currentTime.tv_sec * 1000000LL)
         + currentTime.tv_usec;
}

// =====================================
// MP3 COMMAND FUNCTION
// =====================================

void sendCommand(uint8_t command, uint16_t parameter) {

  uint8_t paramHigh = (parameter >> 8) & 0xFF;
  uint8_t paramLow = parameter & 0xFF;

  uint16_t checksum = 0xFFFF -
    (0xFF + 0x06 + command + 0x01 +
     paramHigh + paramLow) + 1;

  uint8_t commandBuffer[10] = {

    0x7E,
    0xFF,
    0x06,
    command,
    0x01,
    paramHigh,
    paramLow,
    (uint8_t)(checksum >> 8),
    (uint8_t)(checksum & 0xFF),
    0xEF
  };

  MP3_SERIAL.write(commandBuffer, 10);
}

// =====================================
// AUDIO PLAY FUNCTION
// =====================================

void playAudio() {

  Serial.print("Playing track ");
  Serial.println(currentTrack);

  sendCommand(0x03, currentTrack);

  audioPlaying = true;
}

// =====================================
// AUDIO TRACK SWITCHING
// =====================================

void selectTrack(int track) {

  if (track != 1 && track != 2) {
    return;
  }

  currentTrack = track;

  Serial.print("Selected track: ");
  Serial.println(currentTrack);

  // If timer is running, only select the track
  if (timerRunning) {

    Serial.println("Track queued");

  }

  // If timer is idle, switch audio immediately
  else {

    playAudio();

  }
}

// =====================================
// AUDIO STOP FUNCTION
// =====================================

void stopAudio() {

  if (audioPlaying) {

    sendCommand(0x16, 0);

    audioPlaying = false;

    Serial.println("Audio Stopped!");

  }
}

// =====================================
// VOLUME CONTROL FUNCTION
// =====================================

void changeVolume(int amount) {

  int newVolume = volume + amount;

  // Limit volume
  if (newVolume > MAX_VOLUME) {
    newVolume = MAX_VOLUME;
  }

  if (newVolume < MIN_VOLUME) {
    newVolume = MIN_VOLUME;
  }

  // Only send command if volume changes
  if (newVolume != volume) {

    volume = newVolume;

    sendCommand(0x06, volume);

    Serial.print("Volume: ");
    Serial.println(volume);

  }

  else {

    Serial.println("Volume limit reached.");

  }
}

// =====================================
// STOP FUNCTION
// =====================================

void stopSystem() {

  if (timerRunning) {

    timerRunning = false;

    Serial.println();
    Serial.println("Timer Stopped!");

    Serial.println("Enter another timer length in seconds:");

  }

  if (audioPlaying) {

    stopAudio();

  }
}

// =====================================
// SERIAL INPUT FUNCTION
// =====================================

void checkSerialInput() {

  while (Serial.available() > 0) {

    char input = Serial.read();

    // Stop timer or audio
    if (input == 's' || input == 'S') {

      stopSystem();

    }

    // Decrease volume
    else if (input == '-' || input == '_') {

      changeVolume(-VOLUME_STEP);

    }

    // Increase volume
    else if (input == '=' || input == '+') {

      changeVolume(VOLUME_STEP);

    }

    // Select track 1 using T1
    else if (input == 'T' || input == 't') {

      if (Serial.available() > 0) {

        char nextChar = Serial.read();

        if (nextChar == '1') {

          selectTrack(1);

        }

        else if (nextChar == '2') {

          selectTrack(2);

        }

      }

    }

    // Start timer when idle
    else if (input >= '0' && input <= '9') {

      if (!timerRunning) {

        // Parse remaining digits
        long enteredTime = input - '0';

        while (Serial.available() > 0) {

          char nextChar = Serial.peek();

          if (nextChar >= '0' && nextChar <= '9') {

            enteredTime = enteredTime * 10 +
                          (Serial.read() - '0');

          }

          else if (nextChar == '\n' || nextChar == '\r') {

            Serial.read();

          }

          else {

            break;

          }

        }

        timerLength = enteredTime;

        if (timerLength > 0) {

          startTime = getRTCtime();

          previousTime = -1;

          timerRunning = true;

          Serial.println("Timer Started!");

        }

      }

    }

  }
}

// =====================================
// SETUP
// =====================================

void setupAudio() {

  Serial.begin(115200);

  while (!Serial) {
    delay(10);
  }

  // Initialize MP3 UART
  MP3_SERIAL.begin(9600, SERIAL_8N1, MP3_RX, MP3_TX);

  delay(1000);

  // Select SD card
  sendCommand(0x09, 0x0002);

  delay(500);

  // Set starting volume
  sendCommand(0x06, volume);

  Serial.println();
  Serial.println("ESP32-S3 RTC TIMER");
  Serial.println("------------------------");

  Serial.println("MP3 Module Initialized");

  Serial.println("Starting Volume: 20");

  Serial.println();
  Serial.println("CONTROLS");

  Serial.println("Stop Timer / Audio: S");

  Serial.println("Volume Down: -");
  Serial.println("Volume Up: +");

  Serial.println("Select Track 1: T1");
  Serial.println("Select Track 2: T2");

  Serial.println();
  Serial.println("Enter timer length in seconds:");

}

// =====================================
// MAIN LOOP
// =====================================

void timerRun() {

  // Always check keyboard input
  checkSerialInput();

  // ===================================
  // TIMER FUNCTION
  // ===================================

  if (timerRunning) {

    // Calculate elapsed time
    int64_t elapsedMicroseconds = getRTCtime() - startTime;

    long elapsedSeconds = elapsedMicroseconds / 1000000LL;

    // Calculate remaining time
    long remainingTime = timerLength - elapsedSeconds;

    // Display remaining time once per second
    if (elapsedSeconds != previousTime &&
        elapsedSeconds < timerLength) {

      previousTime = elapsedSeconds;

      Serial.print("Time Remaining: ");

      Serial.print(remainingTime);

      Serial.println(" seconds");

    }

    // =================================
    // TIMER COMPLETION
    // =================================

    if (elapsedSeconds >= timerLength) {

      Serial.println();

      Serial.println("========================");
      Serial.println("       TIME IS UP!");
      Serial.println("========================");

      timerRunning = false;

      // Play currently selected track
      playAudio();

      Serial.println();
      Serial.println("Enter another timer length in seconds:");

    }
  }
}

