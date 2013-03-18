/************************************************************************************
* IMBRAX - INDUSTRIA MECATRONICA BRASILEIRA
* Name: CosmClient
* Description: Code redesigned to send some sensor data to Cosm account, used a BMP085
* board from adafruit to read altitude, pressure and temperature, also used a DHT22
* board from seeedstudio to read temperature and relative humidity. These sensors are
* sampled every 6 seconds, after 10 reads the mean is calculated and sent to Cosm.
* This code is based on the example that comes with Arduino IDE, Adafruit_BMP085 and
* DHT libraries were used to read from sensor boards.
*
* Written by: Diego W. Antunes <diego@imbrax.com.br>
* Date: 10/03/2013
*************************************************************************************/

#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <DHT.h>
#include <SPI.h>
#include <Ethernet.h>

/* Uncomment the line below to have some Debug messages on the serial line */
/* #define DEBUG 1 */

/************************************************************************************
* Constants
************************************************************************************/
#define APIKEY     "API KEY HERE"  /* replace your cosm api key here */
#define FEEDID     0000            /* replace your feed ID           */
#define USERAGENT  "Your Project"  /* user agent is the project name */
#define DHTTYPE    DHT22           /* DHT22  (AM2302)                */

static const uint8_t DHTPIN = 2;     /* Arduino pin number used to read DHT22 data */
/* Pins A4 and A5 are used to communicate with the BMP085 board through I2C */

static const uint8_t MAX_DATA_SIZE = 5;
static const uint8_t g_u8MaxSample = 9;

/************************************************************************************
* Structs and Unions
************************************************************************************/
typedef enum {
    DATA_TYPE_NOTYPE = 0,
    DATA_TYPE_INT,
    DATA_TYPE_FLOAT,
    DATA_TYPE_LONG,
} DATA_TYPE_t;

typedef struct {
    void     *dataPointer;
    uint8_t  dataType;
    char     dataName[2];  
} myData_t;

/************************************************************************************
* Objects
************************************************************************************/
DHT dht(DHTPIN, DHTTYPE);
Adafruit_BMP085 bmp;
EthernetClient client;
const IPAddress ip(192,168,2,3);
const IPAddress server(216,52,233,121);

/************************************************************************************
* Prototypes
************************************************************************************/
static void readPressureAndAltitude(void);
static void readHumidity(void);
static void sendData(const void *thisData, const uint8_t dataType, const char *variableName);
static uint8_t getLength(const void *someValue, const uint8_t dataType);

/************************************************************************************
* Variables
************************************************************************************/
static bool g_bSendToCosm = false;
static uint8_t mac[6] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
static float g_fMeanAltitude[2] = {0.0, 0.0};
static float g_fMeanTemperature[2] = {0.0, 0.0};
static float g_fMeanRealAltitude[2] = {0.0, 0.0};
static float g_fMedia_humidade[2] = {0.0, 0.0};
static float g_fMedia_temperatura[2] = {0.0, 0.0};
static int32_t g_i32MeanPressure[2] = {0, 0};
static unsigned long g_ulLastReadTime = 0;
static const unsigned long g_ulReadTime = 6 * 1000UL;
static myData_t myData[MAX_DATA_SIZE];

/************************************************************************************
* Function: void setup(void)
* Description: Function where pins are defined as Input/Output, Serial port initialized
*              ethernet interface gets ip address over dhcp if available, sensors are
*              initialized and struct is filled with arrays address etc.
* Notes: 
************************************************************************************/
void setup(void)
{
#ifdef DEBUG
    Serial.begin(9600);
#endif
    if (Ethernet.begin(mac) == 0) {
#ifdef DEBUG
        Serial.println("Failed using DHCP");
#endif        
        /* DHCP failed, so use a fixed IP address. */
        Ethernet.begin(mac, ip);
    }

#ifdef DEBUG
    Serial.print("IP: ");    
    Serial.println(Ethernet.localIP());
#endif 
    if (!dht.begin()) {
#ifdef DEBUG
        Serial.println("Could not find DHT22!");
#endif
    	while (1) {}
    }
  
    if (!bmp.begin()) {
#ifdef DEBUG
        Serial.println("Could not find BMP085!");
#endif    
    	while (1) {}
    }
    
    for (uint8_t idx = 0; idx < MAX_DATA_SIZE; idx++) {
        myData[idx].dataPointer = 0;
        myData[idx].dataType = DATA_TYPE_NOTYPE;
        memset(myData[idx].dataName, 0, 2);
    }
    
    myData[0].dataPointer = &g_fMeanTemperature[1];
    myData[0].dataType = DATA_TYPE_FLOAT;
    myData[0].dataName[0] = '1';
    
    myData[1].dataPointer = &g_i32MeanPressure[1];
    myData[1].dataType = DATA_TYPE_LONG;
    myData[1].dataName[0] = '2';

    myData[2].dataPointer = &g_fMeanRealAltitude[1];
    myData[2].dataType = DATA_TYPE_FLOAT;
    myData[2].dataName[0] = '3';

    myData[3].dataPointer = &g_fMedia_humidade[1];
    myData[3].dataType = DATA_TYPE_FLOAT;
    myData[3].dataName[0] = '4';

    myData[4].dataPointer = &g_fMedia_temperatura[1];
    myData[4].dataType = DATA_TYPE_FLOAT;
    myData[4].dataName[0] = '5';
}

/************************************************************************************
* Function: void loop(void)
* Description: Where the fun stuff goes, checks if its time to sample data and
*              controls if needs to open connection with server. Also calls the
*              function to send data over Ethernet.
* Notes: 
************************************************************************************/
void loop(void)
{
    static uint8_t idx = 0;
    static bool bEthIsConnected = false;

    if ((g_bSendToCosm == true)) {        
        if (bEthIsConnected == true) {
            if (idx == MAX_DATA_SIZE) {
                g_bSendToCosm = false;
                idx = 0;
                client.stop();
                bEthIsConnected = false;
#ifdef DEBUG
                Serial.println("disconnecting");
#endif
            } else {
                sendData(myData[idx].dataPointer, myData[idx].dataType, myData[idx].dataName);                
                idx++;                
            }
        } else {
            if (client.connect(server, 80)) {
                bEthIsConnected = true;
#ifdef DEBUG
                Serial.println("connecting ok.");
#endif
            } else {
                g_bSendToCosm = false;
                /* if you couldn't make a connection */
                client.stop();
                bEthIsConnected = false;
#ifdef DEBUG                
                Serial.println("connection failed\n disconnecting.");
#endif
            }
        }    
    }

    if (millis() - g_ulLastReadTime > g_ulReadTime) {
        readHumidity();
        readPressureAndAltitude();        
        g_ulLastReadTime = millis();
    }
}

/************************************************************************************
* Function: static void sendData(const void *thisData, const uint8_t dataType,
*            const char *variableName)
* Description: This method sends data to the server via HTTP connection.
* Notes: 
************************************************************************************/
static void sendData(const void *thisData, const uint8_t dataType, const char *variableName)
{
    uint16_t thisLength = 0;
#ifdef DEBUG
    Serial.println("Sending data...");
#endif

    /* calculate the length of the sensor reading in bytes + number of digits of the data */
    if (dataType == DATA_TYPE_FLOAT) {
        thisLength = getLength(thisData, dataType);
        thisLength += 3; // (.) e duas casas decimais
    } else if (dataType == DATA_TYPE_LONG) {
        thisLength = getLength(thisData, dataType);
    }
    thisLength = thisLength + strlen(variableName) + 1;

    /* send the HTTP PUT request */
    client.print("PUT /v2/feeds/");
    client.print(FEEDID);
    client.println(".csv HTTP/1.1");
    client.println("Host: api.cosm.com");
    client.print("X-PachubeApiKey: ");
    client.println(APIKEY);
    client.print("User-Agent: ");
    client.println(USERAGENT);
    client.print("Content-Length: ");
    client.println(thisLength);
    /* last pieces of the HTTP PUT request: */
    client.println("Content-Type: text/csv");
    client.println("Connection: keep-alive\n");

    /* here's the actual content of the PUT request */
    client.print(variableName);
    client.print(",");

    if (dataType == DATA_TYPE_FLOAT) {
        client.println(*((float *)thisData));
    }  else if (dataType == DATA_TYPE_LONG) {
        client.println(*((int32_t *)thisData));
    }

#ifdef DEBUG
    delay(200);
    int toRead = client.available();
    while (toRead != 0) {
        char c = client.read();
        Serial.print(c);
        --toRead;
    }
#endif
}

/************************************************************************************
* Function: static uint8_t getLength(const void *someValue, const uint8_t dataType)
* Description: This method calculates the number of digits in the sensor reading.
* Notes: Since each digit of the ASCII decimal representation is a byte, the number
*        of digits equals the number of bytes.
************************************************************************************/
static uint8_t getLength(const void *someValue, const uint8_t dataType)
{
    /* there's at least one byte */
    uint8_t digits = 1;
  
    /* continually divide the value by ten, adding one to the digit count for each
       time you divide, until you're at 0 */
    if (dataType == DATA_TYPE_FLOAT) {
        float dividend = 0.0;
        dividend = *((float *)someValue);
        dividend /= 10;

        while (dividend > 0 && dividend > 1.0) {
            dividend = dividend /10;
            digits++;
        }
    } else if (dataType == DATA_TYPE_LONG) {
        int32_t dividend = 0;
        dividend = *((int32_t *)someValue);
        dividend /= 10;

        while (dividend > 0) {
            dividend = dividend /10;
            digits++;
        }
    }

    return digits;
}

/************************************************************************************
* Function: static void readHumidity(void)
* Description: Read humidity and temperature on the DHT22 sensor.
* Notes: Samples are read every 6 seconds, at the end of minute mean is calculated.
************************************************************************************/
static void readHumidity(void)
{
    static uint8_t u8Index = 0;

    /* Reading temperature or humidity takes about 250 milliseconds!
       Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor) */
    float h = dht.readHumidity();
    float t = dht.readTemperature();

    /* check if returns are valid, if they are NaN (not a number) then something went wrong! */
    if (!isnan(t) && !isnan(h)) {
        g_fMedia_humidade[0] += h;
        g_fMedia_temperatura[0] += t;
        
        if (g_u8MaxSample == u8Index) {
            g_fMedia_humidade[1] = g_fMedia_humidade[0] / 10;
            g_fMedia_temperatura[1] = g_fMedia_temperatura[0] / 10;
#ifdef DEBUG
            Serial.print("Humidity: ");            
            Serial.print(g_fMedia_humidade[1]);
            Serial.print(" %\t");
            Serial.print("Temperature: ");
            Serial.print(g_fMedia_temperatura[1]);
            Serial.println(" *C");
#endif
            g_fMedia_humidade[0] = 0.0;
            g_fMedia_temperatura[0] = 0.0;
            u8Index = 0;
        } else {
            ++u8Index;
        }
    } else {
#ifdef DEBUG        
        Serial.println("Failed to read from DHT");
#endif        
    }
}

/************************************************************************************
* Function: static void readPressureAndAltitude(void)
* Description: Reads temperatura, pressure and altitude from the BMP 085 sensor.
* Notes: Samples are read every 6 seconds, at the end of minute mean is calculated.
************************************************************************************/
static void readPressureAndAltitude(void)
{
    static uint8_t counter = 0;
    
    g_fMeanTemperature[0] += bmp.readTemperature();
    g_i32MeanPressure[0] += bmp.readPressure();
    g_fMeanAltitude[0] += bmp.readAltitude();            
    g_fMeanRealAltitude[0] += bmp.readAltitude(101325);
        
    if (g_u8MaxSample == counter) {
        g_fMeanTemperature[1] = g_fMeanTemperature[0] / 10;
        g_i32MeanPressure[1] = g_i32MeanPressure[0] / 10;
        g_fMeanAltitude[1] = g_fMeanAltitude[0] / 10;
        g_fMeanRealAltitude[1] = g_fMeanRealAltitude[0] / 10;

#ifdef DEBUG
        Serial.print("Temperature = ");
        Serial.print(g_fMeanTemperature[1]);
        Serial.print(" *C \t");
        Serial.print("Pressure = ");
        Serial.print(g_i32MeanPressure[1]);
        Serial.print(" Pa \t");
        Serial.print("Altitude = ");
        Serial.print(g_fMeanAltitude[1]);
        Serial.print(" m \t");
        Serial.print("Real altitude = ");
        Serial.print(g_fMeanRealAltitude[1]);
        Serial.println(" m");
#endif
        g_bSendToCosm = true;
        g_fMeanAltitude[0] = 0.0;
        g_i32MeanPressure[0] = 0;
        g_fMeanTemperature[0] = 0.0;
        g_fMeanRealAltitude[0] = 0.0;
        counter = 0;
    } else {
        counter++;
    }
}
