#define VERSION "26.3.15-1"

/*
 *     English: Pulse counter recorder
 *     Français : Enregistreur d'impulsions
 *
 *  Available URL
 *      /           Root page
 *      /status     Returns status in JSON format
 *      /setup      Display setup page
 *      /command    Supports the following commands:
 *                      enable enter - disable enter
 *                      enable debug - disable debug
 *                      enable verbose - disable verbose
 *                      enable java - disable java
 *                      enable syslog - disable syslog
 *      /languages  Return list of supported languages
 *      /settings   Returns settings in JSON format
 *      /debug      Display internal variables to debug
 *      /log        Return saved log
 *      /edit       Manage and edit file system
 *      /changed    Change a variable value (internal use only)
 *      /rest       Execute API commands
 *          /restart
 *                      Restart ESP
 *
 *  URL disponibles
 *      /           Page d'accueil
 *      /status     Retourne l'état sous forme JSON
 *      /setup      Affiche la page de configuration
 *      /command    Supporte les commandes suivantes :
 *                      enable enter - disable enter
 *                      enable debug - disable debug
 *                      enable verbose - disable verbose
 *                      enable java - disable java
 *                      enable syslog - disable syslog
 *      /languages  Retourne la liste des langues supportées
 *      /settings   Retourne la configuration au format JSON
 *      /debug      Affiche les variables internes pour déverminer
 *      /log        Retourne le log mémorisé
 *      /edit       Gère et édite le système de fichier
 *      /changed    Change la valeur d'une variable (utilisation interne)
 *      /rest       Exécute une commande de type API
 *          /restart
 *                      Redémarre l'ESP
 *
 *  Flying Domotic - March 2026
 *
 *  GNU GENERAL PUBLIC LICENSE - Version 3, 29 June 2007
 *
 */

#include <arduino.h>                                                // Arduino
#include <ArduinoJson.h>                                            // JSON documents

#ifdef ESP32
    #include <getChipId.h>                                          // ESP.getChipId emulation
    #if CONFIG_IDF_TARGET_ESP32
        #include "esp32/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32S2
        #include "esp32s2/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C2
        #include "esp32c2/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C3
        #include "esp32c3/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32S3
        #include "esp32s3/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32C6
        #include "esp32c6/rom/rtc.h"
    #elif CONFIG_IDF_TARGET_ESP32H2
        #include "esp32h2/rom/rtc.h"
    #else
        #error Target CONFIG_IDF_TARGET is not supported
    #endif
#endif

//  ---- Macros ----
#undef __FILENAME__                                                 // Deactivate standard macro, only supporting "/" as separator
#define __FILENAME__ (strrchr(__FILE__, '\\')? strrchr(__FILE__, '\\')+1 : (strrchr(__FILE__, '/')? strrchr(__FILE__, '/')+1 : __FILE__))

//          -------------------
//          ---- Variables ----
//          -------------------

//  ---- WiFi ----
#ifdef ESP32
    #include <WiFi.h>                                               // WiFi
#else
    #include <ESP8266WiFi.h>                                        // WiFi
#endif

//  ---- Recorder ----

unsigned long recordingStartTime = 0;                               // Recording start time (as millis())
unsigned long recordingStopTime = 0;                                // Recording stop time
unsigned long lastCollectTime = 0;                                  // Time of last record
unsigned long lastCollectCounter = 0;                               // Pulse counter @ lastCollectTime
unsigned long lastElapsedTime = 0;                                  // Last elapsed time
unsigned long lastElapsedCounter = 0;                               // Last elapsed counter
unsigned long lastFileWrite = 0;                                    // Last time we wrote the file
unsigned long recordCount = 0;                                      // Count of written records
struct tm startTimeInfo;                                            // Recording start time (as tm)
bool startTimeInfoValid = false;                                    // Is recording start time valid?
char collectFileName[33];                                           // Collect file name
bool collectFileInited = false;                                     // File has already been created
bool firstPulseSeen = false;                                        // At least one pulse seen flag
bool automaticMode = true;                                          // Automatic mode armed
bool inFirstLoop = true;                                            // We're in first loop flag
uint16_t noPulseCurrentCount = 0;                                   // Count of collects without pulse

// Saved values
struct memoryArray_t {
    unsigned long elapsedTime;                                      // Elapsed time since start of recording
    unsigned long elapsedValue;                                     // Value associated with this elapsed time
};

#define MEMORY_ARRAY_SIZE 300
memoryArray_t memoryArray[MEMORY_ARRAY_SIZE];                       // Memory array to store values
uint16_t memoryPtr = 0;                                             // Pointer to memory array

//  ---- Used by interrupts ----
volatile unsigned long pulseCounter = 0;                            // Pulse counter
volatile bool isRecording = false;                                  // Is recording flag

//  ---- Log ----
#ifndef LOG_MAX_LINES
    #ifdef ESP32
        #define LOG_MAX_LINES 15
        #define LOG_LINE_LEN 100
    #endif
    #ifdef ESP8266
        #define LOG_MAX_LINES 5
        #define LOG_LINE_LEN 100
    #endif
#endif

char emptyChar[] = "";                                              // Empty string
char savedLogLines[LOG_MAX_LINES][LOG_LINE_LEN];                    // Buffer to save last log lines
uint16_t savedLogNextSlot = 0;                                      // Address of next slot to be written
uint16_t logRequestNextLog = 0;                                     // Address of next slot to be send for a /log request

//  ---- Syslog ----
#ifdef FF_TRACE_USE_SYSLOG
    #include <Syslog.h>                                             // Syslog client https://github.com/arcao/Syslog
    #include <WiFiUdp.h>
    WiFiUDP udpClient;
    Syslog syslog(udpClient, SYSLOG_PROTO_IETF);
    unsigned long lastSyslogMessageMicro = 0;                       // Date of last syslog message (microseconds)
#endif

//  ---- Asynchronous web server ----
#ifdef ESP32
    #include <AsyncTCP.h>                                           // Asynchronous TCP
#else
    #include <ESPAsyncTCP.h>                                        // Asynchronous TCP
#endif
#include <ESPAsyncWebServer.h>                                      // Asynchronous web server
#include <LittleFS.h>                                               // Flash file system
#include <littleFsEditor.h>                                         // LittleFS file system editor
AsyncWebServer webServer(80);                                       // Web server on port 80
AsyncEventSource events("/events");                                 // Event root
String lastUploadedFile = emptyChar;                                // Name of last download file
int lastUploadStatus = 0;                                           // HTTP last error code
FSInfo littleFsInfo;                                                // LittleFs info
uint8_t flashUsedPercent = 255;                                     // Little FS used space percentage

//  ---- Preferences ----
#define SETTINGS_FILE "/settings.json"

String ssid;                                                        // SSID of local network
String pwd;                                                         // Password of local network
String accessPointPwd;                                              // Access point password
#ifdef VERSION_FRANCAISE
    String espName = "Enregistreur";                                // Name of this module
#else
    String espName = "Recorder";                                    // Name of this module
#endif
String hostName;                                                    // Host name
String serverLanguage;                                              // This server language
String syslogServer;                                                // Syslog server name or IP (can be empty)
uint16_t syslogPort;                                                // Syslog port (default to 514)
String ntpServer;                                                   // NTP server nale or IP (can be empty)
String ntpParameters;                                               // NTP parameters
unsigned long collectInterval;                                      // Interval between 2 data collection (ms)
unsigned long fileWriteInterval;                                    // Interval between 2 file write (ms)
unsigned long noPulseCollectCount;                                  // Count of collect without pulse before stopping
bool traceEnter = true;                                             // Trace routine enter?
bool traceDebug = true;                                             // Trace debug messages?
bool traceVerbose = false;                                          // Trace (super) verbose?
bool traceJava = false;                                             // Trace Java code?
bool traceSyslog = false;                                           // Send traces to syslog?

//  ---- Local to this program ----
String resetCause = emptyChar;                                      // Used to save reset cause
bool sendAnUpdateFlag = false;                                      // Should we send an update?
String wifiState = emptyChar;                                       // Wifi connection state

//  ---- Serial commands ----
#ifdef SERIAL_COMMANDS
    char serialCommand[100];                                        // Buffer to save serial commands
    size_t serialCommandLen = 0;                                    // Buffer used lenght
#endif

bool restartMe = false;                                             // Ask for code restart

//          --------------------------------------
//          ---- Function/routines definition ----
//          --------------------------------------

//  ---- Recorder routines ----

void pulseSetup(void);
void pulseLoop(void);
void startRecording(void);
void stopRecording(void);
void setCollectFileName(void);
void updateCounters(void);
void writeFile(void);

//  ---- Interrupt routines ----

IRAM_ATTR void pulseDownCallback();

//  ---- Time routines----

void timeSetup(void);
String getTime(unsigned long waitTime = 0);

//  ---- WiFi routines ----

#ifdef ESP32
    void onWiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onWiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
    void onWiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info);
#endif

#ifdef ESP8266
    WiFiEventHandler onStationModeConnectedHandler;                 // Event handler called when WiFi is connected
    WiFiEventHandler onStationModeDisconnectedHandler;              // Event handler called when WiFi is disconnected
    WiFiEventHandler onStationModeGotIPHandler;                     // Event handler called when WiFi got an IP
#endif

//  ---- Log routines ----

void saveLogMessage(const char* message);
String getLogLine(const uint16_t line, const bool reversedOrder = false);
void logSetup(void);
void syslogSetup(void);

//  ---- Serial commands ----

#ifdef SERIAL_COMMANDS
    void serialLoop(void);
#endif

//  ---- Trace rountines ----

#include <FF_Trace.h>                                               // Trace module https://github.com/FlyingDomotic/FF_Trace
trace_callback(traceCallback);
void traceSetup(void);
void enterRoutine(const char* routineName);

//  ---- System routines ----

#ifdef ESP32
    String verbosePrintResetReason(int3 reason);
#endif

void readLittleFsInfo(void);
String getResetCause(void);

//  ---- Preferences routines ----

void restartToApply(void);
void dumpSettings(void);
bool readSettings(void);
void writeSettings(void);

//  ---- Web server routines ----

void percentDecode(char *src);
int parseUrlParams(char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl);
void setupReceived(AsyncWebServerRequest *request);
void restReceived(AsyncWebServerRequest *request);
void settingsReceived(AsyncWebServerRequest *request);
void debugReceived(AsyncWebServerRequest *request);
void statusReceived(AsyncWebServerRequest *request);
void setChangedReceived(AsyncWebServerRequest *request);
void languagesReceived(AsyncWebServerRequest *request);
void commandReceived(AsyncWebServerRequest *request);
void logReceived(AsyncWebServerRequest *request);
void notFound(AsyncWebServerRequest *request);
void updateWebServerData(void);
void sendWebServerUpdate(void);

//  ---- OTA routines ----

#include <ArduinoOTA.h>                                             // OTA (network update)

void onStartOTA(void);
void onEndOTA(void);
void onErrorOTA(const ota_error_t erreur);

// --- User's routines ---

bool inString(const String candidate, const String listOfValues, const String separator = ",");
String extractItem(const String candidate, const uint16_t index, const String separator = ",");
void checkFreeBufferSpace(const char *function, const uint16_t line, const char *bufferName,
    const size_t bufferSize, const size_t bufferLen);
bool isDebugCommand(const String givenCommand);

//  ---- Recording routines ----

// Init pulse data
void pulseSetup(void) {
    pinMode(INTERRUPT_PIN, INPUT);                                  // Set INPUT mode (external 10K pullup)
    #ifdef INTERRUPT_PIN_LEVEL_LOW                                  // Is pin active at low state?
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), pulseDownCallback, FALLING);
    #else                                                           // Pin is active at high state
        attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), pulseDownCallback, RISING);
    #endif
}

// Pulse loop
void pulseLoop(void) {
    if (isRecording) {                                              // Are we recording?
        if ((millis() - lastCollectTime) >= collectInterval) {      // Should we update counters?
            updateCounters();                                       // Collect counters
            sendAnUpdateFlag = true;                                // Update web server
        }
        if ((millis() - lastFileWrite) >= fileWriteInterval) {      // Should we write file?
            writeFile();                                            // Write file
        }
    }
}

// Activate recording
void startRecording(void) {
    if (traceEnter) enterRoutine(__func__);
    firstPulseSeen = false;                                         // No pulse seen
    lastCollectCounter = 0;                                         // Last written counter
    lastFileWrite = recordingStartTime;                             // Last file write time
    lastCollectTime = recordingStartTime;                           // Last collect time
    noPulseCurrentCount = 0;                                        // Réinit current nopulse count
    lastElapsedTime = 0;                                            // Last elapsed time
    lastElapsedCounter = 0;                                         // ... and counter
    recordCount = 0;                                                // Clear record count
    collectFileInited = false;                                      // File not inited, header needed
    recordingStartTime = 0;                                         // Clear start time
    noInterrupts();                                                 // Stop intetrrupts
    pulseCounter = 0;                                               // Reset pluse counter
    isRecording = true;                                             // Set recording flag
    interrupts();                                                   // Enable interrupts
    #ifdef VERSION_FRANCAISE
        trace_info_P("Début d'enregistrement demandé", NULL);
    #else
        trace_info_P("Start recording asked", NULL);
    #endif
}

// Stop recording
void stopRecording(void) {
    if (traceEnter) enterRoutine(__func__);
    if (isRecording) {                                              // Do this only is recording is active
        noInterrupts();                                             // Stop intetrrupts
        isRecording = false;                                        // Set recording flag
        recordingStopTime = millis();                               // Save stop time
        interrupts();                                               // Enable interrupts
        updateCounters();                                           // Update counter (last time)
        writeFile();                                                // Force file write
        #ifdef VERSION_FRANCAISE
            trace_info_P("Fin d'enregistrement dans %s, %lu enregistrements écrits",
                collectFileName, recordCount);
        #else
            trace_info_P("Stop recording in %s, %lu records written",
                collectFileName, recordCount);
        #endif
    }
    if (automaticMode) {                                            // Are we in automatic mode?
        startRecording();                                           // Start recording requested
    }
}

// Set collect file name
void setCollectFileName(void) {
    if (startTimeInfoValid) {                                       // If start time info is valid
        strftime(collectFileName, sizeof(collectFileName),          // Use start time time (as tm)
            "%Y%m%d_%H%M%S.txt", &startTimeInfo);                   // ... to define file name
    } else {                                                        // Time not set by NTP
        for (uint8_t i = 1; i < 100; i++) {                         // Try from 1 to 99
            snprintf_P(collectFileName, sizeof(collectFileName),
                "collect%02d.txt", i);                              // Use collect + serial number
            if (!LittleFS.exists(collectFileName)) {                // If file doen't exist
                return;                                             // Keep name
            }
        }
    // Return constantly "collect100.txt"
    }
}

// Update counters
void updateCounters(void) {
    if (traceEnter) enterRoutine(__func__);
    unsigned long currentCounter = pulseCounter;                    // Save current counter
    unsigned long now = millis();
    lastElapsedCounter = pulseCounter - lastCollectCounter;         // Elapsed value is delta since previous round
    lastElapsedTime = now - lastCollectTime;                        // Elapsed time since last round
    lastCollectTime = now;                                          // Save last time we collect counters
    memoryArray[memoryPtr].elapsedValue = lastElapsedCounter;
    memoryArray[memoryPtr].elapsedTime = lastCollectTime - recordingStartTime;
    if (memoryArray[memoryPtr].elapsedValue) {                      // Did we got a pulse in this interval?
        noPulseCurrentCount = 0;                                    // No, reset count
        if (!firstPulseSeen) {                                      // Is this first time we saw a pulse?
            #ifdef VERSION_FRANCAISE
                trace_info_P("Première impulsion détectée !", NULL);
            #else
                trace_info_P("First pulse seen!", NULL);
            #endif
            firstPulseSeen = true;                                  // We saw at least a pulse
        }
    } else if (noPulseCollectCount && firstPulseSeen) {             // No pulse count set and a pulse seen?        noPulseCurrentCount++;
        noPulseCurrentCount++;                                      // One more no pulse seen count
        if (noPulseCurrentCount >= noPulseCollectCount) {           // Did we exceed no pulse count?
            #ifdef VERSION_FRANCAISE
                trace_info_P("Plus d'impulsions pendant %d cycles", noPulseCurrentCount);
            #else
                trace_info_P("No more pulses since %d cycles", noPulseCurrentCount);
            #endif
            stopRecording();                                        // Stop recording
            return;                                                 // Exit
        }
    }
    memoryPtr++;                                                    // Increment memory pointer
    lastCollectCounter = currentCounter;                            // Save current counter for next round
    if (memoryPtr >= MEMORY_ARRAY_SIZE) {                           // Write file if memory array is full
        writeFile();
    }
}

// Write pending counters to file
void writeFile(void) {
    if (traceEnter) enterRoutine(__func__);
    lastFileWrite = millis();                                       // Save last write date
    if (memoryPtr) {                                                // Do we have something to write?
        if (!collectFileInited) {
            setCollectFileName();                                   // Compose file name
            #ifdef VERSION_FRANCAISE
                trace_info_P("Ecriture dans %s", collectFileName);
            #else
                trace_info_P("Writing into %s", collectFileName);
            #endif
            File outStream = LittleFS.open(collectFileName, "w");   // Open collect file
            if (!outStream) {                                       // Error opening?
                #ifdef VERSION_FRANCAISE
                    trace_error_P("Ne peut ouvrir %s en écriture", collectFileName);
                #else
                    trace_error_P("Can't open %s for write", collectFileName);
                #endif
                memoryPtr = 0;                                      // Reset memory array pointer
                return;
            }
            outStream.printf("Milliseconds;Pulses\n");              // Write header
            outStream.flush();                                      // Flush file
            outStream.close();                                      // Close file
            collectFileInited = true;                               // We inited file
        }
        File outStream = LittleFS.open(collectFileName, "a");       // Open collect file
        if (!outStream) {                                           // Error opening?
            #ifdef VERSION_FRANCAISE
                trace_error_P("Ne peut ouvrir %s en ajout", collectFileName);
            #else
                trace_error_P("Can't open %s for append", collectFileName);
            #endif
            memoryPtr = 0;                                          // Reset memory array pointer
            return;
        }
        for (uint16_t i = 0; i < memoryPtr; i++) {                  // Dump all stored data
            outStream.printf_P("%lu;%lu\n",                         // Compose line
                memoryArray[i].elapsedTime,
                memoryArray[i].elapsedValue);
            recordCount++;                                          // Update record count
        }
        outStream.flush();                                          // Flush file
        outStream.close();                                          // Close file
        memoryPtr = 0;                                              // Reset memory array pointer
        readLittleFsInfo();                                         // Reload FS info
    }
}

//  ---- Interrupt routines ----

// Routine called each time there's an arm interrupt on switch
IRAM_ATTR void pulseDownCallback() {
    if (isRecording) {                                              // If recording is active
        if (!pulseCounter) {                                        // First pulse
            recordingStartTime = millis();                          // Save millis
            startTimeInfoValid = getLocalTime(&startTimeInfo, 0);   // And absolute time
        }
        pulseCounter++;                                             // ... then increment counter
    }
}

//  ---- Time ----

// Setup time server
void timeSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    if (ntpServer != "") {
        if (ntpParameters != "") {
            #ifdef ESP32
                configTime(0, 0, ntpServer.c_str());
                setenv("TZ",ntpParameters.c_str(),1);
                tzset();
            #endif
            #ifdef ESP8266
                configTime(ntpParameters.c_str(), ntpServer.c_str());
            #endif
        }
    }
}

//  Return current local time as String
String getTime(unsigned long waitTime) {
    struct tm timeinfo;
    getLocalTime(&timeinfo, waitTime);
    char dateStr[25];
    strftime(dateStr, sizeof(dateStr), "%Y/%m/%d %H:%M:%S", &timeinfo);
    return String(dateStr);
}

//  ---- WiFi ----

// Called when wifi is connected
#ifdef ESP32
    void onWiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif
#ifdef ESP8266
    void onWiFiStationConnected(WiFiEventStationModeConnected data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    char buffer[100];
    #ifdef VERSION_FRANCAISE
        snprintf_P(buffer, sizeof(buffer), "Connecté à %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #else
        snprintf_P(buffer, sizeof(buffer), "Connected to %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #endif
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    wifiState = String(buffer);
    updateWebServerData();
}

// Called when an IP is given
#ifdef ESP32
    void onWiFiStationGotIp(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif
#ifdef ESP8266
    void onWiFiStationGotIp(WiFiEventStationModeGotIP data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    char buffer[100];
    #ifdef VERSION_FRANCAISE
        snprintf_P(buffer, sizeof(buffer), "Connecté à %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #else
        snprintf_P(buffer, sizeof(buffer), "Connected to %s (%s)",
            ssid.c_str(), WiFi.localIP().toString().c_str());
    #endif
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    wifiState = String(buffer);
    updateWebServerData();
}

// Called when WiFi is disconnected
#ifdef ESP32
    void onWiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
#endif

#ifdef ESP8266
    void onWiFiStationDisconnected(WiFiEventStationModeDisconnected data) {
#endif
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Wifi déconnecté", NULL);
    #else
        trace_info_P("Wifi disconnected", NULL);
    #endif
}

//  ---- Log routines ----

// Save a message to log queue
void saveLogMessage(const char* message) {
    strncpy(savedLogLines[savedLogNextSlot++], message, LOG_LINE_LEN-1);
    if (savedLogNextSlot >= LOG_MAX_LINES) {
        savedLogNextSlot = 0;
    }
}

// Returns a log line number
String getLogLine(const uint16_t line, const bool reversedOrder) {
    int16_t ptr = savedLogNextSlot;
    if (reversedOrder) {
        ptr -= line+1;
        if (ptr < 0) {
            ptr += (LOG_MAX_LINES-1);
        }
    } else {
        ptr += line+1;
        if (ptr >= LOG_MAX_LINES) {
            ptr -= (LOG_MAX_LINES-1);
        }
    }
    if (ptr >=0 && ptr < LOG_MAX_LINES) {
        return savedLogLines[ptr];
    }
    return String("");
}

// Setup part for log
void logSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    // Clear all log slots
    for (uint16_t i = 0; i < LOG_MAX_LINES; i++) {
        memset(savedLogLines[i], 0, LOG_LINE_LEN);
    }
}

// Setup part of syslog
void syslogSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef FF_TRACE_USE_SYSLOG
        if (syslogServer != "") {
            syslog.server(syslogServer.c_str(), syslogPort);
        }
        syslog.defaultPriority(LOG_LOCAL0 + LOG_DEBUG);
        syslog.appName(__FILENAME__);
    #endif
}

//  ---- Serial commands ----
#ifdef SERIAL_COMMANDS
    // Manage Serial commands
    void serialLoop(void) {
        while(Serial.available()>0) {
            char c = Serial.read();
            // Check for end of line
            if (c == '\n' || c== '\r') {
                // Do we have some command?
                if (serialCommandLen) {
                    #ifdef VERSION_FRANCAISE
                        Serial.printf("Commande : >%s<\n", serialCommand);
                    #else
                        Serial.printf("Command: >%s<\n", serialCommand);
                    #endif
                    String command = serialCommand;
                    if (isDebugCommand(command)) {
                        // Command is known and already executed, do nothing
                    } else {
                        #ifdef VERSION_FRANCAISE
                            Serial.println(PSTR("Utiliser enable/disable trace/debug/enter/syslog"));
                    #else
                            Serial.println(PSTR("Use enable/disable trace/debug/enter/syslog"));
                        #endif
                    }
                }
                // Reset command
                serialCommandLen = 0;
                serialCommand[serialCommandLen] = '\0';
            } else {
                // Do we still have room in buffer?
                if (serialCommandLen < sizeof(serialCommand)) {
                    // Yes, add char
                    serialCommand[serialCommandLen++] = c;
                    serialCommand[serialCommandLen] = '\0';
                } else {
                    // Reset command
                    serialCommandLen = 0;
                    serialCommand[serialCommandLen] = '\0';
                }
            }
        }
    }
#endif

//  ---- Trace routines ----

trace_declare();                                                    // Declare trace class

// Trace callback routine
//    _level contains severity level
//    _file: calling source file name with extension (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _line: calling source file line (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _function: calling calling source function name (unless FF_TRACE_NO_SOURCE_INFO is defined)
//    _message contains message to display/send

trace_callback(traceCallback) {
    //String messageLevel = FF_TRACE.textLevel(_level);
    if (_level != FF_TRACE_LEVEL_DEBUG || traceDebug) {             // Don't trace debug if debug flag not set
        Serial.print(FF_TRACE.textLevel(_level));
        Serial.print(": ");
        Serial.println(_message);                                   // Print message on Serial
        #ifdef SERIAL_FLUSH
            Serial.flush();
        #endif
        if (_level == FF_TRACE_LEVEL_ERROR || _level == FF_TRACE_LEVEL_WARN) {
            events.send(_message, "error");                         // Send message to destination
        } else if (_level != FF_TRACE_LEVEL_NONE) {
            if (events.count() && (events.avgPacketsWaiting() < 5)) {// If any clients connected and less than 5 packets pending
                events.send(_message, "info");                      // Send message to destination
            }
        }
        // Send trace to syslog if needed
        #ifdef FF_TRACE_USE_SYSLOG
            #define MIN_MICROS 1000
            if (syslogServer != "" && WiFi.status() == WL_CONNECTED && traceSyslog) {
                unsigned long currentMicros = micros();             // Get microseconds
                if ((currentMicros - lastSyslogMessageMicro) < MIN_MICROS) {  // Last message less than a ms
                    delayMicroseconds(MIN_MICROS - (currentMicros - lastSyslogMessageMicro)); // Wait remaining ms                            // Delay a ms to avoid overflow
                }
                syslog.deviceHostname(messageLevel.c_str());
                switch(_level) {
                    case FF_TRACE_LEVEL_ERROR:
                        syslog.log(LOG_ERR, _message);
                        break;
                    case FF_TRACE_LEVEL_WARN:
                        syslog.log(LOG_WARNING, _message);
                        break;
                    case FF_TRACE_LEVEL_INFO:
                        syslog.log(LOG_INFO, _message);
                        break;
                    default:
                        syslog.log(LOG_DEBUG, _message);
                        break;
                }
                lastSyslogMessageMicro = micros();                  // Save date of last syslog message in microseconds
            }
        #endif
        saveLogMessage(_message);                                   // Save message into circular log
    }
}

//  Trace setup code
void traceSetup(void) {
    if (traceEnter) enterRoutine(__func__);
    trace_register(&traceCallback);                                 // Register callback
    FF_TRACE.setLevel(FF_TRACE_LEVEL_VERBOSE);                      // Start with verbose trace
}

//  Trace each routine entering
void enterRoutine(const char* routineName) {
    #ifdef VERSION_FRANCAISE
        trace_info_P("Entre dans %s", routineName);
    #else
        trace_info_P("Entering %s", routineName);
    #endif
}

//  ---- System routines ----

// Read LittleFs info
void readLittleFsInfo(void) {
    LittleFS.info(littleFsInfo);
    flashUsedPercent = littleFsInfo.totalBytes? littleFsInfo.usedBytes * 100 / littleFsInfo.totalBytes : 255;
}

// Return ESP32 reset reason text
#ifdef ESP32
    String verbosePrintResetReason(int reason) {
        switch ( reason) {
            case 1  : return PSTR("Vbat power on reset");break;
            case 3  : return PSTR("Software reset digital core");break;
            case 4  : return PSTR("Legacy watch dog reset digital core");break;
            case 5  : return PSTR("Deep Sleep reset digital core");break;
            case 6  : return PSTR("Reset by SLC module, reset digital core");break;
            case 7  : return PSTR("Timer Group0 Watch dog reset digital core");break;
            case 8  : return PSTR("Timer Group1 Watch dog reset digital core");break;
            case 9  : return PSTR("RTC Watch dog Reset digital core");break;
            case 10 : return PSTR("Instrusion tested to reset CPU");break;
            case 11 : return PSTR("Time Group reset CPU");break;
            case 12 : return PSTR("Software reset CPU");break;
            case 13 : return PSTR("RTC Watch dog Reset CPU");break;
            case 14 : return PSTR("for APP CPU, reseted by PRO CPU");break;
            case 15 : return PSTR("Reset when the vdd voltage is not stable");break;
            case 16 : return PSTR("RTC Watch dog reset digital core and rtc module");break;
            default : return PSTR("Can't decode reason ")+String(reason);
        }
    }
#endif

// Return ESP reset/restart cause
String getResetCause(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef ESP32
        #ifdef VERSION_FRANCAISE
            String reason = "Raison reset : CPU#0: "+verbosePrintResetReason(rtc_get_reset_reason(0))
                +", CPU#1: "+verbosePrintResetReason(rtc_get_reset_reason(1));
        #else
            String reason = "Reset reasons: CPU#0: "+verbosePrintResetReason(rtc_get_reset_reason(0))
                +", CPU#1: "+verbosePrintResetReason(rtc_get_reset_reason(1));
        #endif
        return reason;
    #else
        struct rst_info *rtc_info = system_get_rst_info();
        // Get reset reason
        #ifdef VERSION_FRANCAISE
            String reason = PSTR("Raison reset : ") + String(rtc_info->reason, HEX)
                + PSTR(" - ") + ESP.getResetReason();
        #else
            String reason = PSTR("Reset reason: ") + String(rtc_info->reason, HEX)
                + PSTR(" - ") + ESP.getResetReason();
        #endif
        // In case of software restart, send additional info
        if (rtc_info->reason == REASON_WDT_RST
                || rtc_info->reason == REASON_EXCEPTION_RST
                || rtc_info->reason == REASON_SOFT_WDT_RST) {
            // If crashed, print exception
            if (rtc_info->reason == REASON_EXCEPTION_RST) {
                reason += PSTR(", exception (") + String(rtc_info->exccause)+PSTR("):");
            }
            reason += PSTR(" epc1=0x") + String(rtc_info->epc1, HEX)
                    + PSTR(", epc2=0x") + String(rtc_info->epc2, HEX)
                    + PSTR(", epc3=0x") + String(rtc_info->epc3, HEX)
                    + PSTR(", excvaddr=0x") + String(rtc_info->excvaddr, HEX)
                    + PSTR(", depc=0x") + String(rtc_info->depc, HEX);
        }
        return reason;
    #endif
}

//  ---- Preferences routines ----

// Dumps all settings on screen
void dumpSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    trace_info_P("ssid = %s", ssid.c_str());
    trace_info_P("pwd = %s", pwd.c_str());
    trace_info_P("accessPointPwd = %s", accessPointPwd.c_str());
    trace_info_P("name = %s", espName.c_str());
    trace_info_P("traceEnter = %s", traceEnter? "true" : "false");
    trace_info_P("traceDebug = %s", traceDebug? "true" : "false");
    trace_info_P("traceVerbose = %s", traceVerbose? "true" : "false");
    trace_info_P("traceJava = %s", traceJava? "true" : "false");
    trace_info_P("traceSyslog = %s", traceSyslog? "true" : "false");
    trace_info_P("ntpServer = %s", ntpServer.c_str());
    trace_info_P("ntpParameters = %s", ntpParameters.c_str());
    trace_info_P("serverLanguage = %s", serverLanguage.c_str());
    trace_info_P("syslogServer = %s", syslogServer.c_str());
    trace_info_P("syslogPort = %d", syslogPort);
    trace_info_P("collectInterval = %lu", collectInterval);
    trace_info_P("fileWriteInterval = %lu", fileWriteInterval);
    trace_info_P("noPulseCollectCount = %lu", noPulseCollectCount);
}

// Restart to apply message
void restartToApply(void) {
    #ifdef VERSION_FRANCAISE
        trace_info_P("*** Relancer l'ESP pour prise en compte ***", NULL);
    #else
        trace_info_P("*** Restart ESP to apply changes ***", NULL);
    #endif
}

// Read settings
bool readSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    File settingsFile = LittleFS.open(SETTINGS_FILE, "r");          // Open settings file
    if (!settingsFile) {                                            // Error opening?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut ouvrir %s", SETTINGS_FILE);
        #else
            trace_error_P("Failed to %s", SETTINGS_FILE);
        #endif
        return false;
    }

    JsonDocument settings;
    auto error = deserializeJson(settings, settingsFile);           // Read settings
    settingsFile.close();                                           // Close file
    if (error) {                                                    // Error reading JSON?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut décoder %s", SETTINGS_FILE);
        #else
            trace_error_P("Failed to parse %s", SETTINGS_FILE);
        #endif
        return false;
    }

    // Load all settings into corresponding variables
    traceEnter = settings["traceEnter"].as<bool>();
    traceDebug = settings["traceDebug"].as<bool>();
    traceVerbose = settings["traceVerbose"].as<bool>();
    traceJava = settings["traceJava"].as<bool>();
    traceSyslog = settings["traceSyslog"].as<bool>();
    ssid = settings["ssid"].as<String>();
    pwd = settings["pwd"].as<String>();
    accessPointPwd = settings["accessPointPwd"].as<String>();
    espName = settings["name"].as<String>();
    serverLanguage = settings["serverLanguage"].as<String>();
    syslogServer = settings["syslogServer"].as<String>();
    syslogPort = settings["syslogPort"].as<uint16_t>();
    ntpServer = settings["ntpServer"].as<String>();
    ntpParameters = settings["ntpParameters"].as<String>();
    collectInterval = settings["collectInterval"].as<uint32_t>();
    fileWriteInterval = settings["fileWriteInterval"].as<uint32_t>();
    noPulseCollectCount = settings["noPulseCollectCount"].as<uint32_t>();

    // Use syslog port default value if needed
    if (syslogPort == 0) {
        syslogPort = 514;
    }

    // Dump settings on screen
    dumpSettings();
    return true;
}

// Write settings
void writeSettings(void) {
    if (traceEnter) enterRoutine(__func__);
    JsonDocument settings;

    // Load settings in JSON
    settings["ssid"] = ssid.c_str();
    settings["pwd"] = pwd.c_str();
    settings["accessPointPwd"] = accessPointPwd.c_str();
    settings["name"] = espName.c_str();
    settings["traceEnter"] = traceEnter;
    settings["traceDebug"] = traceDebug;
    settings["traceVerbose"] = traceVerbose;
    settings["traceJava"] = traceJava;
    settings["traceSyslog"] = traceSyslog;
    settings["serverLanguage"] = serverLanguage.c_str();
    settings["syslogServer"] = syslogServer.c_str();
    settings["syslogPort"] = syslogPort;
    settings["ntpServer"] = ntpServer.c_str();
    settings["ntpParameters"] = ntpParameters.c_str();
    settings["collectInterval"] = collectInterval;
    settings["fileWriteInterval"] = fileWriteInterval;
    settings["noPulseCollectCount"] = noPulseCollectCount;

    File settingsFile = LittleFS.open(SETTINGS_FILE, "w");          // Open settings file
    if (!settingsFile) {                                            // Error opening?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut ouvrir %s en écriture", SETTINGS_FILE);
        #else
            trace_error_P("Can't open %s for write", SETTINGS_FILE);
        #endif
        return;
    }

    uint16_t bytes = serializeJsonPretty(settings, settingsFile);   // Write JSON structure to file
    if (!bytes) {                                                   // Error writting?
        #ifdef VERSION_FRANCAISE
            trace_error_P("Ne peut écrire %s", SETTINGS_FILE);
        #else
            trace_error_P("Can't write %s", SETTINGS_FILE);
        #endif
    }
    settingsFile.flush();                                           // Flush file
    settingsFile.close();                                           // Close it
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Envoi settings", NULL);
    #else
        trace_debug_P("Sending settings event", NULL);
    #endif
    events.send("Ok", "settings");                                  // Send a "settings" (changed) event
}

//  ---- Web server routines ----

//  Perform URL percent decoding
void percentDecode(char *src) {
    char *dst = src;
    while (*src) {
        if (*src == '+') {
            src++;
            *dst++ = ' ';
        } else if (*src == '%') {
            // handle percent escape
            *dst = '\0';
            src++;
            if (*src >= '0' && *src <= '9') {*dst = *src++ - '0';}
            else if (*src >= 'A' && *src <= 'F') {*dst = 10 + *src++ - 'A';}
            else if (*src >= 'a' && *src <= 'f') {*dst = 10 + *src++ - 'a';}
            *dst <<= 4;
            if (*src >= '0' && *src <= '9') {*dst |= *src++ - '0';}
            else if (*src >= 'A' && *src <= 'F') {*dst |= 10 + *src++ - 'A';}
            else if (*src >= 'a' && *src <= 'f') {*dst |= 10 + *src++ - 'a';}
            dst++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

//  Parse an URL parameters list and return each parameter and value in a given table
int parseUrlParams (char *queryString, char *results[][2], const int resultsMaxCt, const boolean decodeUrl) {
    int ct = 0;

    while (queryString && *queryString && ct < resultsMaxCt) {
    results[ct][0] = strsep(&queryString, "&");
    results[ct][1] = strchr(results[ct][0], '=');
    if (*results[ct][1]) *results[ct][1]++ = '\0';
    if (decodeUrl) {
        percentDecode(results[ct][0]);
        percentDecode(results[ct][1]);
    }
    ct++;
    }
    return ct;
}

// Called when /setup is received
void setupReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, "setup.htm", "text/html");
    request->send(response);                                        // Send setup.htm
}

// Called when /rest is received
void restReceived (AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    if (request->url() == "/rest/restart") {
        request->send(200, "text/plain", "Restarting...");
        restartMe = true;
        return;
    }
}

// Called when /settings is received
void settingsReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginResponse(LittleFS, SETTINGS_FILE, "application/json");
    request->send(response);                                        // Send settings.json
}

// Called when /debug is received
void debugReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    // Send a json document with interresting variables
    JsonDocument answer;
    answer["version"] = VERSION;
    answer["wifiState"] = wifiState.c_str();
    answer["collectFileName"] = collectFileName;
    answer["collectFileInited"] = collectFileInited;
    answer["recordingStartTime"] = recordingStartTime;
    answer["pulseCounter"] = pulseCounter;
    answer["lastElapsedTime"] = lastElapsedTime;
    answer["lastElapsedCounter"] = lastElapsedCounter;
    answer["memoryPtr"] = memoryPtr;
    answer["firstPulseSeen"] = firstPulseSeen;
    answer["traceEnter"] = traceEnter;
    answer["traceDebug"] = traceDebug;
    answer["traceVerbose"] = traceVerbose;
    answer["traceJava"] = traceJava;
    #ifdef ESP32
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxAllocHeap();
        answer["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    String buffer;
    serializeJsonPretty(answer, buffer);
    request->send(200, "application/json", buffer);
}

// Called when a /status click is received
void statusReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    // Send a json document with data correponding to current status
    JsonDocument answer;
    char buffer[512];
    #ifdef ESP32
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxAllocHeap();
        answer["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        answer["freeMemory"] = ESP.getFreeHeap();
        answer["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    serializeJsonPretty(answer, buffer, sizeof(buffer));
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    request->send(200, "application/json", buffer);
}

// Called when /changed/<variable name>/<variable value> is received
void setChangedReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    bool dontWriteSettings = false;
    String position = request->url().substring(1);
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Reçu %s", position.c_str());
    #else
        trace_debug_P("Received %s", position.c_str());
    #endif
    int separator1 = position.indexOf("/");                         // Position of first "/"
    if (separator1 >= 0) {
        int separator2 = position.indexOf("/", separator1+1);       // Position of second "/"
        if (separator2 > 0) {
            // Extract field name and value
            String fieldName = position.substring(separator1+1, separator2);
            String fieldValue = position.substring(separator2+1);
            // Check against known field names and set value accordingly
            if (fieldName == "traceEnter") {
                traceEnter = (fieldValue == "true");
            } else if (fieldName == "traceDebug") {
                traceDebug = (fieldValue == "true");
            } else if (fieldName == "traceVerbose") {
                traceVerbose = (fieldValue == "true");
            } else if (fieldName == "traceJava") {
                traceJava = (fieldValue == "true");
            } else if (fieldName == "traceSyslog") {
                traceSyslog = (fieldValue == "true");
            } else if (fieldName == "ssid") {
                ssid = fieldValue;
                restartToApply();
            } else if (fieldName == "pwd") {
                restartToApply();
                pwd = fieldValue;
            } else if (fieldName == "accessPointPwd") {
                restartToApply();
                accessPointPwd = fieldValue;
            } else if (fieldName == "name") {
                restartToApply();
                espName = fieldValue;
            } else if (fieldName == "serverLanguage") {
                serverLanguage = fieldValue;
            } else if (fieldName == "syslogServer") {
                #ifdef FF_TRACE_USE_SYSLOG
                    if (fieldValue != "") {
                        if (syslogServer != fieldValue) {
                            syslog.server(fieldValue.c_str(), syslogPort);
                        }
                    }
                #endif
                syslogServer = fieldValue;
            } else if (fieldName == "syslogPort") {
                #ifdef FF_TRACE_USE_SYSLOG
                    if (fieldValue.toInt() > 0 && syslogServer != "") {
                        if (atol(fieldValue.c_str()) >= 0 && atol(fieldValue.c_str()) <= 65535) {
                            if (syslogPort != fieldValue.toInt()) {
                                syslog.server(syslogServer.c_str(), fieldValue.toInt());
                            }
                        }
                    }
                #endif
                syslogPort = fieldValue.toInt();
            } else if (fieldName == "ntpServer") {
                ntpServer = fieldValue;
                timeSetup();
            } else if (fieldName == "ntpParameters") {
                ntpParameters = fieldValue;
                timeSetup();
            } else if (fieldName == "collectInterval") {
                collectInterval = fieldValue.toInt();
            } else if (fieldName == "fileWriteInterval") {
                fileWriteInterval = fieldValue.toInt();
            } else if (fieldName == "noPulseCollectCount") {
                noPulseCollectCount = fieldValue.toInt();
            } else if (fieldName == "start") {
                automaticMode = false;
                startRecording();
                dontWriteSettings = true;
            } else if (fieldName == "stop") {
                automaticMode = false;
                stopRecording();
                dontWriteSettings = true;
            } else if (fieldName == "restart") {
                restartMe = true;
            } else {
                // This is not a known field
                #ifdef VERSION_FRANCAISE
                    trace_error_P("Donnée >%s< inconnue, valeur >%s<", fieldName.c_str(), fieldValue.c_str());
                #else
                    trace_error_P("Can't set field >%s<, value >%s<", fieldName.c_str(), fieldValue.c_str());
                #endif
                char msg[70];                                       // Buffer for message
                snprintf_P(msg, sizeof(msg),PSTR("<status>Bad field name %s</status>"), fieldName.c_str());
                checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
                request->send(400, emptyChar, msg);
                return;
            }
            if (!dontWriteSettings) writeSettings();
        } else {
            // This is not a known field
            #ifdef VERSION_FRANCAISE
                trace_error_P("Pas de nom de donnée", NULL);
            #else
                trace_error_P("No field name", NULL);
            #endif
            char msg[70];                                           // Buffer for message
            snprintf_P(msg, sizeof(msg),PSTR("<status>No field name</status>"));
            checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
            request->send(400, emptyChar, msg);
            return;
        }
    }
    request->send(200, emptyChar, "<status>Ok</status>");
}

// Called when /languages command is received
void languagesReceived(AsyncWebServerRequest *request){
    if (traceEnter) enterRoutine(__func__);
    String path = "/";
#ifdef ESP32
    File dir = LittleFS.open(path);
#else
    Dir dir = LittleFS.openDir(path);
    path = String();
#endif
    String output = "[";
#ifdef ESP32
    File entry = dir.openNextFile();
    while(entry){
#else
    while(dir.next()){
        fs::File entry = dir.openFile("r");
#endif
        String fileName = String(entry.name());
        if (fileName.startsWith("lang_")) {
            #ifdef ESP32
                fileName = path + fileName;
            #endif
            File languageFile = LittleFS.open(fileName, "r");       // Open language file
            if (languageFile) {                                     // Open ok?
                JsonDocument jsonData;
                auto error = deserializeJson(jsonData, languageFile); // Read settings
                languageFile.close();                               // Close file
                if (!error) {                                       // Reading JSON ok?
                    if (output != "[") output += ',';
                    output += "{\"code\":\"";
                    output += jsonData["code"].as<String>();
                    output += "\",\"text\":\"";
                    output += jsonData["text"].as<String>();
                    output += "\"}";
                } else {
                    #ifdef VERSION_FRANCAISE
                        trace_error_P("Ne peut decoder %s", fileName.c_str());
                    #else
                        trace_error_P("Can't decode %s", fileName.c_str());
                    #endif
                }
            } else {
                #ifdef VERSION_FRANCAISE
                    trace_error_P("Ne peut ouvrir %s", fileName.c_str());
                #else
                    trace_error_P("Can't open %s", fileName.c_str());
                #endif
            }
        }
        #ifdef ESP32
            entry = dir.openNextFile();
        #else
            entry.close();
        #endif
        }
#ifdef ESP32
    dir.close();
#endif
    output += "]";
    request->send(200, "application/json", output);
}

// Called when /command/<command name>/<commandValue> is received
void commandReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    String position = request->url().substring(1);
    #ifdef VERSION_FRANCAISE
        trace_debug_P("Reçu %s", position.c_str());
    #else
        trace_debug_P("Received %s", position.c_str());
    #endif
    String commandName = emptyChar;
    String commandValue = emptyChar;
    int separator1 = position.indexOf("/");                         // Position of first "/"
    if (separator1 >= 0) {
        int separator2 = position.indexOf("/", separator1+1);       // Position of second "/"
        if (separator2 > 0) {
            // Extract field name and value
            commandName = position.substring(separator1+1, separator2);
            commandValue = position.substring(separator2+1);
        } else {
            commandName = position.substring(separator1+1);
        }
        // Check against known command names
        if (commandName == "xxx") {
        } else {
            // This is not a known field
            #ifdef VERSION_FRANCAISE
                trace_error_P("Commande >%s< inconnue", commandName.c_str());
            #else
                trace_error_P("Can't execute command >%s<", commandName.c_str());
            #endif
            char msg[70];                                           // Buffer for message
            snprintf_P(msg, sizeof(msg),PSTR("<status>Bad command name %s</status>"), commandName.c_str());
            checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
            request->send(400, emptyChar, msg);
            return;
        }
    } else {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Pas de commande", NULL);
        #else
            trace_error_P("No command name", NULL);
        #endif
        char msg[70];                                               // Buffer for message
        snprintf_P(msg, sizeof(msg),PSTR("<status>No command name</status>"));
        checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
        request->send(400, emptyChar, msg);
        return;
    }
    request->send(200, emptyChar, "<status>Ok</status>");
}

// Called when /log is received - Send saved log, line by line
void logReceived(AsyncWebServerRequest *request) {
    if (traceEnter) enterRoutine(__func__);
    AsyncWebServerResponse *response = request->beginChunkedResponse("text/plain; charset=utf-8",
            [](uint8_t *logResponseBuffer, size_t maxLen, size_t index) -> size_t {
        // For all log lines
        while (logRequestNextLog < LOG_MAX_LINES) {
            // Get message
            String message = getLogLine(logRequestNextLog++);
            // If not empty
            if (message != emptyChar) {
                // Compute message len (adding a "\n" at end)
                size_t chunkSize = min(message.length(), maxLen-1)+1;
                // Copy message
                memcpy(logResponseBuffer, message.c_str(), chunkSize-1);
                // Add "\n" at end
                logResponseBuffer[chunkSize-1] = '\n';
                // Return size (and message loaded)
                return chunkSize;
            }
        }
        // That's the end
        return 0;
    });
    logRequestNextLog = 0;
    request->send(response);
}

// Called when a request can't be mapped to existing ones
void notFound(AsyncWebServerRequest *request) {
    char msg[120];
    #ifdef VERSION_FRANCAISE
        snprintf_P(msg, sizeof(msg), PSTR("Fichier %s inconnu"), request->url().c_str());
    #else
        snprintf_P(msg, sizeof(msg), PSTR("File %s not found"), request->url().c_str());
    #endif
    trace_debug(msg);
    checkFreeBufferSpace(__func__, __LINE__, "msg", sizeof(msg), strlen(msg));
    request->send(404, "text/plain", msg);
    trace_info(msg);
}

//  ---- OTA routines ----

// Called when OTA starts
void onStartOTA(void) {
    if (traceEnter) enterRoutine(__func__);
    if (ArduinoOTA.getCommand() == U_FLASH) {                       // Program update
        #ifdef VERSION_FRANCAISE
            trace_info_P("Début MAJ firmware", NULL);
        #else
            trace_info_P("Starting firmware update", NULL);
        #endif
    } else {                                                        // File system update
        #ifdef VERSION_FRANCAISE
            trace_info_P("Début MAJ fichiers", NULL);
        #else
            trace_info_P("Starting file system update", NULL);
        #endif
        LittleFS.end();
    }
}

// Called when OTA ends
void onEndOTA(void) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Fin de MAJ", NULL);
    #else
        trace_info_P("End of update", NULL);
    #endif
}

// Called when OTA error occurs
void onErrorOTA(const ota_error_t erreur) {
    if (traceEnter) enterRoutine(__func__);
    #ifdef VERSION_FRANCAISE
        String msg = "Erreur OTA(";
        msg += String(erreur);
        msg += ") : Erreur ";
        if (erreur == OTA_AUTH_ERROR) {
            msg += "authentification";
        } else if (erreur == OTA_BEGIN_ERROR) {
            msg += "lancement";
        } else if (erreur == OTA_CONNECT_ERROR) {
            msg += "connexion";
        } else if (erreur == OTA_RECEIVE_ERROR) {
            msg += "réception";
        } else if (erreur == OTA_END_ERROR) {
            msg += "fin";
        } else {
            msg += "inconnue !";
        }
    #else
        String msg = "OTA error(";
        msg += String(erreur);
        msg += ") : Error ";
        if (erreur == OTA_AUTH_ERROR) {
            msg += "authentication";
        } else if (erreur == OTA_BEGIN_ERROR) {
            msg += "starting";
        } else if (erreur == OTA_CONNECT_ERROR) {
            msg += "connecting";
        } else if (erreur == OTA_RECEIVE_ERROR) {
            msg += "receiving";
        } else if (erreur == OTA_END_ERROR) {
            msg += "terminating";
        } else {
            msg += "unknown !";
        }
    #endif
    trace_error(msg.c_str());
}

// --- User's routines ---

// Looks for string into a list of strings
bool inString(const String candidate, const String listOfValues, const String separator) {
    int endPosition = 0;
    int startPosition = 0;
    String allValues = listOfValues + separator;
    while (endPosition >= 0) {
        endPosition = allValues.indexOf(separator, startPosition);
        // Compare sending number with extracted one
        if (candidate.equalsIgnoreCase(allValues.substring(startPosition, endPosition))) {
            return true;
        }
        startPosition = endPosition+1;
    }
    return false;
}

// Returns part of a string, giving index and delimiter
String extractItem(const String candidate, const uint16_t index, const String separator) {
    int endPosition = 0;
    int startPosition = 0;
    int i = 0;
    String allValues = candidate + separator;
    while (endPosition >= 0) {
        endPosition = allValues.indexOf(separator, startPosition);
        if (i == index) {
            // Return part corresponding to required item
            return allValues.substring(startPosition, endPosition);
        }
        startPosition = endPosition+1;
    }
    return emptyChar;
}

// Check for remaining space into a buffer
void checkFreeBufferSpace(const char *function, const uint16_t line, const char *bufferName,
        const size_t bufferSize, const size_t bufferLen) {
    if ((bufferSize - bufferLen) < 0 || bufferSize <= 0) {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Taille %d et longueur %d pour %s dans %s:%d", bufferSize, bufferLen,
                bufferName, function, line);
        #else
            trace_error_P("Invalid size %d and length %d for %s in %s:%d", bufferSize, bufferLen,
                bufferName, function, line);
        #endif
    } else {
        size_t freeSize = bufferSize - bufferLen;
        uint8_t percent = (bufferLen * 100)/ bufferSize;
        if (percent > 90) {
            #ifdef VERSION_FRANCAISE
                trace_debug_P("%s:%d: %s rempli à %d\%%, %d octets libres (taille %d, longueur %d))",
            #else
                trace_debug_P("%s:%d: %s is %d\%% full, %d bytes remaining (size %d, length %d))",
            #endif
                function, line, bufferName, percent, freeSize, bufferSize, bufferLen);
        }
    }
}

// Execute a debug command, received either by Serial or MQTT
bool isDebugCommand(const String givenCommand) {
    if (traceEnter) enterRoutine(__func__);
    String command = String(givenCommand);
    command.toLowerCase();
    // enable/disable trace/debug
    if (command == "enable debug") {
        traceDebug = true;
    } else if (command == "disable debug") {
        traceDebug = false;
    } else if (command == "enable verbose") {
        traceVerbose = true;
    } else if (command == "disable verbose") {
        traceVerbose = false;
    } else if (command == "enable enter") {
        traceEnter = true;
    } else if (command == "disable enter") {
        traceEnter = false;
    } else if (command == "enable java") {
        traceJava = true;
    } else if (command == "disable java") {
        traceJava = false;
    } else if (command == "enable syslog") {
        traceSyslog = true;
    } else if (command == "disable syslog") {
        traceSyslog = false;
    } else {
        return false;
    }
    return true;
}

// Update state on web server
void updateWebServerData(void) {
    // Flag update needed
    sendAnUpdateFlag = true;
}

// Send web server data to clients
void sendWebServerUpdate(void) {
    if (traceEnter) enterRoutine(__func__);
    char buffer[512];
    // Send new state to connected users
    JsonDocument data;
    data["serverName"] = espName.c_str();
    data["serverVersion"] = VERSION;
    data["wifiState"] = wifiState.c_str();
    data["date"]= getTime().c_str();
    unsigned long delta = recordingStartTime? millis() - recordingStartTime : 0;
    data["elapsedTime"] = delta;
    data["elapsedPulse"] = pulseCounter;
    data["elapsedAverage"] = delta? float(pulseCounter) * 1000.0 / float(delta) : 0.0f;
    data["collectTime"] = lastElapsedTime;
    data["collectPulse"] = lastElapsedCounter;
    data["collectAverage"] = lastElapsedTime? float(lastElapsedCounter) * 1000.0 / float(lastElapsedTime) : 0.0f;
    data["usedPercentage"] = flashUsedPercent;
    data["recordCount"] = recordCount;
    #ifdef ESP32
        data["freeMemory"] = ESP.getFreeHeap();
        data["largestChunk"] = ESP.getMaxAllocHeap();
        data["memoryLowMark"] = ESP.getMinFreeHeap();
    #else
        data["freeMemory"] = ESP.getFreeHeap();
        data["largestChunk"] = ESP.getMaxFreeBlockSize();
    #endif
    serializeJson(data, buffer, sizeof(buffer));
    checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
    events.send(buffer, "data");
    sendAnUpdateFlag = false;
}

// Check if a string starts with another
bool startWith(const char* stringToTest, const char* compareWith) {
    return strncmp(stringToTest, compareWith, strlen(compareWith)) == 0;
}

// Wait until event queue empty for 100 ms max (return true if timeout occured)
bool waitForEventsEmpty(void) {
    int loopCount = 0;
    // Wait for enpty event queue for 100 ms
    while (events.avgPacketsWaiting() && loopCount < 100) {
        delay(1);
        loopCount++;
    }
    return (events.avgPacketsWaiting());
}

//          -------------------------------------
//          ---- Program initialization code ----
//          -------------------------------------

// Setup routine
void setup(void) {
    traceEnter = true;
    if (traceEnter) enterRoutine(__func__);
    logSetup();                                                     // Init log
    traceSetup();                                                   // Register trace
    #ifdef ESP8266
        Serial.begin(74880);
    #else
        Serial.begin(115200);
    #endif
    Serial.setDebugOutput(false);                                   // To allow Serial.swap() to work properly

    Serial.println(emptyChar);
    #ifdef VERSION_FRANCAISE
        trace_info_P("Initialise %s V%s", __FILENAME__, VERSION);
    #else
        trace_info_P("Initializing %s V%s", __FILENAME__, VERSION);
    #endif
    resetCause = getResetCause();                                   // Get reset cause
    trace_info_P("Cause : %s", resetCause.c_str());

    #ifdef ESP32
        // Stop Blutooth
        btStop();
    #endif

    // Starts flash file system
    LittleFS.begin();

    #define DUMP_FILE_SYSTEM
    #ifdef DUMP_FILE_SYSTEM
        String path = "/";
        #ifdef ESP32
            File dir = LittleFS.open(path);
            #ifdef VERSION_FRANCAISE
                trace_info_P("Contenu flash", NULL);
            #else
                trace_info_P("FS content", NULL);
            #endif
            File entry = dir.openNextFile();
            while(entry){
        #else
            Dir dir = LittleFS.openDir(path);
            path = String();
            #ifdef VERSION_FRANCAISE
                trace_info_P("Contenu flash", NULL);
            #else
                trace_info_P("FS content", NULL);
            #endif
            while(dir.next()){
                fs::File entry = dir.openFile("r");
        #endif
                String fileName = String(entry.name());
                #ifdef ESP32
                    fileName = path + fileName;
                #endif
                trace_info_P("%s", fileName.c_str());
                #ifdef ESP32
                    entry = dir.openNextFile();
                #else
                    entry.close();
                #endif
                }
        #ifdef ESP32
            dir.close();
        #endif
    #endif

    // Read LittleFS info
    readLittleFsInfo();

    // Load preferences
    if (!readSettings()) {
        #ifdef VERSION_FRANCAISE
            trace_error_P("Pas de configuration, stop !", NULL);
        #else
            trace_error_P("No settings, stopping!", NULL);
        #endif
        while (true) {
            yield();
        }
    };
    yield();

    hostName = espName;                                             // Set host name to ESP name
    hostName.replace(" ", "-");                                     // Replace spaces by dashes
    WiFi.hostname(hostName.c_str());                                // Define this module name for client network
    WiFi.setAutoReconnect(true);                                    // Reconnect automatically

    #ifdef ESP32
        WiFi.IPv6(false);                                           // Disable IP V6 for AP
    #endif

    #ifdef ESP32
        WiFi.onEvent(onWiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
        WiFi.onEvent(onWiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
        WiFi.onEvent(onWiFiStationGotIp, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
    #endif
    #ifdef  ESP8266
        onStationModeConnectedHandler = WiFi.onStationModeConnected(&onWiFiStationConnected); // Declare connection callback
        onStationModeDisconnectedHandler = WiFi.onStationModeDisconnected(&onWiFiStationDisconnected); // Declare disconnection callback
        onStationModeGotIPHandler = WiFi.onStationModeGotIP(&onWiFiStationGotIp); // Declare got IP callback
    #endif

    ssid.trim();
    if (ssid != emptyChar) {                                        // If SSID is given, try to connect to
        #ifdef ESP32
            WiFi.mode(WIFI_MODE_STA);                               // Set station mode
        #endif
        #ifdef ESP8266
            WiFi.mode(WIFI_STA);                                    // Set station mode
        #endif
        #ifdef VERSION_FRANCAISE
            trace_info_P("Recherche %s", ssid.c_str());
        #else
            trace_info_P("Searching %s", ssid.c_str());
        #endif
        WiFi.begin(ssid.c_str(), pwd.c_str());                      // Start to connect to existing SSID
        uint16_t loopCount = 0;
        while (WiFi.status() != WL_CONNECTED && loopCount < 10) {   // Wait for connection for 10 seconds
            delay(1000);                                            // Wait for 1 s
            loopCount++;
        }                                                           // Loop
        if (WiFi.status() == WL_CONNECTED) {                        // If we're not connected
            #ifdef VERSION_FRANCAISE
                trace_info_P("Connexion à %s par http://%s/ ou http://%s/ ",
                    ssid.c_str(), WiFi.localIP().toString().c_str(), hostName.c_str());
            #else
                trace_info_P("Connect to %s with http://%s/ or http://%s/ ",
                    ssid.c_str(), WiFi.localIP().toString().c_str(), hostName.c_str());
            #endif
        } else {
            #ifdef VERSION_FRANCAISE
                trace_info_P("Pas connecté, passe en mode point d'accès ...", NULL);
            #else
                trace_info_P("Not connected, starting access point...", NULL);
            #endif
        }
    }

    if (WiFi.status() != WL_CONNECTED) {                            // If not connected, start access point
        #ifdef ESP32
            WiFi.mode(WIFI_MODE_AP);                                // Set access point mode
        #endif
        #ifdef ESP8266
            WiFi.mode(WIFI_AP);                                     // Set access point mode
        #endif
        char buffer[80];
        // Load this Wifi access point name as ESP name plus ESP chip Id
        #ifdef ESP32
            snprintf_P(buffer, sizeof(buffer),PSTR("%s_%X"), hostName.c_str(), getChipId());
        #else
            snprintf_P(buffer, sizeof(buffer),PSTR("%s_%X"), hostName.c_str(), ESP.getChipId());
        #endif
        checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
        #ifdef VERSION_FRANCAISE
            trace_info_P("Creation du point d'accès %s (%s)", buffer, accessPointPwd.c_str());
        #else
            trace_info_P("Creating %s access point (%s)", buffer, accessPointPwd.c_str());
        #endif
        WiFi.softAP(buffer, accessPointPwd.c_str());                // Starts Wifi access point
        #ifdef VERSION_FRANCAISE
            trace_info_P("Connexion à %s par http://%s/", buffer, WiFi.softAPIP().toString().c_str());
            snprintf_P(buffer, sizeof(buffer), "Point d'accès %s actif (%s)",
                ssid.c_str(), WiFi.softAPIP().toString().c_str());
        #else
            trace_info_P("Connect to %s with http://%s/", buffer, WiFi.softAPIP().toString().c_str());
            snprintf_P(buffer, sizeof(buffer), "WiFi access point %s active (%s)",
                ssid.c_str(), WiFi.softAPIP().toString().c_str());
        #endif
        checkFreeBufferSpace(__func__, __LINE__, "buffer", sizeof(buffer), strlen(buffer));
        wifiState = String(buffer);
    }

    timeSetup();
    updateWebServerData();

    // Start syslog
    syslogSetup();                                                  // Init log

    // OTA trace
    ArduinoOTA.onStart(onStartOTA);
    ArduinoOTA.onEnd(onEndOTA);
    ArduinoOTA.onError(onErrorOTA);

    //ArduinoOTA.setPassword("my OTA password");                    // Uncomment to set an OTA password
    ArduinoOTA.begin();                                             // Initialize OTA

    #ifdef VERSION_FRANCAISE
        trace_info_P("%s V%s lancé", __FILENAME__, VERSION);
        trace_info_P("Cause : %s", resetCause.c_str());
    #else
        trace_info_P("Starting %s V%s", __FILENAME__, VERSION);
        trace_info_P("Reset cause: %s", resetCause.c_str());
    #endif

    // List of URL to be intercepted and treated locally before a standard treatment
    //  These URL can be used as API
    webServer.on("/status", HTTP_GET, statusReceived);              // /status request
    webServer.on("/setup", HTTP_GET, setupReceived);                // /setup request
    webServer.on("/command", HTTP_GET, commandReceived);            // /command request
    webServer.on("/languages", HTTP_GET, languagesReceived);        // /languages request
    webServer.on("/settings", HTTP_GET, settingsReceived);          // /settings request
    webServer.on("/debug", HTTP_GET, debugReceived);                // /debug request
    webServer.on("/rest", HTTP_GET, restReceived);                  // /rest request
    webServer.on("/log", HTTP_GET, logReceived);                    // /log request
    // These URL are used internally by setup.htm - Use them at your own risk!
    webServer.on("/changed", HTTP_GET, setChangedReceived);         // /changed request

    // Other webserver stuff
    webServer.addHandler(&events);                                  // Define web events
    webServer.addHandler(new LittleFSEditor());                     // Define file system editor
    webServer.serveStatic("/", LittleFS, "/").setDefaultFile("index.htm"); // Serve "/", default page = index.htm
    webServer.onNotFound (notFound);                                // To be called when URL is not known

    events.onConnect([](AsyncEventSourceClient *client){            // Routine called when a client connects
        #ifdef VERSION_FRANCAISE
            trace_debug_P("Client connecté", NULL);
        #else
            trace_debug_P("Event client connected", NULL);
        #endif
        // Set send an update flag
        sendAnUpdateFlag = true;
        // Send last log lines
        for (uint16_t i=0; i < LOG_MAX_LINES; i++) {
            String logLine = getLogLine(i, true);
            if (logLine != emptyChar) {
                client->send(logLine, "info");
            }
        }
        char msg[20];
        snprintf_P(msg, sizeof(msg),"%016lx", millis());
        client->send(msg, "time");
    });

    events.onDisconnect([](AsyncEventSourceClient *client){         // Routine called when a client connects
        #ifdef VERSION_FRANCAISE
            trace_debug_P("Client déconnecté", NULL);
        #else
            trace_debug_P("Event client disconnected", NULL);
        #endif
    });

    webServer.begin();                                              // Start Web server
    pulseSetup();                                                   // Setup pulse

    #ifdef VERSION_FRANCAISE
        trace_info_P("Fin lancement", NULL);
    #else
        trace_info_P("Init done", NULL);
    #endif
}

//      ------------------------
//      ---- Permanent loop ----
//      ------------------------

// Main loop
void loop(void) {
    if (inFirstLoop) {                                              // Is this the first loop?
        inFirstLoop = false;
        String time = getTime(5000);                                // Wait for NTP sync for 5 seconds
        startRecording();                                           // Start recording data
        delay(1);
    }
    pulseLoop();                                                    // Do pulse stuff
    ArduinoOTA.handle();                                            // Give hand to OTA
    #ifdef SERIAL_COMMANDS
        serialLoop();                                               // Scan for serial commands
    #endif
    // Send an update to clients if needed
    if (sendAnUpdateFlag) {
        sendWebServerUpdate();                                      // Send updated data to clients
    }
    #ifdef FF_TRACE_USE_SYSLOG
        if ((micros() - lastSyslogMessageMicro) > 600000000) {      // Last syslog older than 10 minutes?
            #ifdef VERSION_FRANCAISE
               trace_info_P("Toujours vivant ...", NULL);
            #else
               trace_info_P("I'm still alive...", NULL);
            #endif
        }
    #endif
    if (restartMe) {
        #ifdef VERSION_FRANCAISE
            trace_info_P("Relance l'ESP ...", NULL);
        #else
            trace_info_P("Restarting ESP ...", NULL);
        #endif
        delay(1000);
        ESP.restart();
    }
    delay(1);
}