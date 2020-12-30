
/*
  'Logging' is the logging interface for all components in this project.
*/

#ifndef Logging_h
#define Logging_h
#endif

#define DEFAULT_SERIAL_SPEED 115200


// Use a namespace to prevent clashes with other libraries.
namespace myns {

class Logging {
  public:
    Logging();
    ~Logging();

    // Get the loglevel of a subsystem
    uint16_t GetLogLevel(uint16_t);

    // Set the loglevel on the subsystem level
    void SetLogLevel(int, uint16_t);

    // Log some some text from a particular subsystem when the loglevel equals 'logLevel'
    void Log(uint16_t, uint16_t, const char *);

    // Log some some text and an integer from a particular subsystem when the loglevel equals 'logLevel'
    void Log(uint16_t, uint16_t, const char *, int);

    // Disable logging globally
    void LogGlobalOff();

    // Enable logging globally
    void LogGlobalOn();

    // Return status of global logging
    uint16_t LogGlobalState();

  private:
    // Indicates whether logging is on (1) or off (0)
    uint16_t logGlobal;

    // log level configuration
    // array indices coincide with the subsystem identifier, and the values
    // coincide with the log level.
    // 10 substems should hold us over for now
    uint16_t logLevelConfig[10];

};

} // namespace

/* END */
