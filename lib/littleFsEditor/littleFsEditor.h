#ifndef LittleFSEditor_H_
#define LittleFSEditor_H_
#include <ESPASyncWebServer.h>
#include <LittleFS.h>                                               // Gestion du système de fichier

class LittleFSEditor: public AsyncWebHandler {
  private:
    String _username;
    String _password;
    bool _authenticated;
    uint32_t _startTime;
  public:
#ifdef ESP32
    LittleFSEditor(const String& username=String(), const String& password=String());
    virtual bool canHandle(AsyncWebServerRequest *request) const override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    virtual bool isRequestHandlerTrivial() const override final {return false;}
#else
    LittleFSEditor(const String& username=String(), const String& password=String(), const fs::FS& fs=LittleFS);
    virtual bool canHandle(AsyncWebServerRequest *request) const override final;
    virtual void handleRequest(AsyncWebServerRequest *request) override final;
    virtual void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override final;
    virtual bool isRequestHandlerTrivial() const override final {return false;}
    #define FILE_READ "r"
#endif
};

#endif