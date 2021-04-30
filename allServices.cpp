////
//// Created by Shani du Plessis on 29/04/2021.
////

extern "C" {
    #include <wiringPi.h>
    #include <stdio.h>
    #include <stdlib.h>
    #include <string.h>
    #include "MQTTClient.h"
}
#include <csignal>
#include <iostream>
#include "include/rapidjson/document.h"
#include "include/rapidjson/stringbuffer.h"
#include "include/rapidjson/prettywriter.h"
#include "include/rapidjson/writer.h"
#include <chrono>
#include <fstream>


//using namespace std;
using namespace rapidjson;
using namespace std::chrono;

// variables for all

volatile MQTTClient_deliveryToken deliveredtoken;
char* ADDRESS;

// LED variables

#define CLIENTID_LED    "ledSubscriber"
#define TOPIC_LED       "LED"
#define QOS              0

int pin_LED;
bool led_status;
std::string session_status;
int num_messages = 0;
auto start_led = high_resolution_clock::now(); // initialize start

// PIR variables

#define CLIENTID_PIR    "pir_client"
#define TOPIC_PIR       "PIR"
#define TIMEOUT     10000L
#define PIN_PIR 17


// Humidity and Temperature variables

#define CLIENTID_HT   "hum_temp_client"
#define TOPIC_T       "Temperature"
#define TOPIC_H       "Humidity"

#define MAXTIMINGS	85
#define DHTPIN		7


int dht11_dat[5] = { 0, 0, 0, 0, 0 }; //first 8bits is for humidity integral value, second 8bits for humidity decimal, third for temp integral, fourth for temperature decimal and last for checksum

// ------ LED code ------ //
void delivered(void *context, MQTTClient_deliveryToken dt) // Required callback
{
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredtoken = dt;
}

int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) // Callback function for when an MQTT message arrives from the broker
{
    num_messages = num_messages + 1;
    if(num_messages == 1){
        start_led = high_resolution_clock::now(); // Starting timer
    }
    int i;
    char* payloadptr; //payload

    payloadptr = (char*)message->payload; //payload converted to char*
    int len = strlen(payloadptr);
    if(payloadptr[len-2] == '}'){ // Fix for a bug in RapidJson
        payloadptr[len-1] = '\0';
    }

    rapidjson::Document document;
    document.Parse(payloadptr); // Parse string to JSON
    if(document.HasMember("Done")){ // Done message is received from publisher when communication ends. This triggers the end of the session and the end of the timer
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        session_status = "Done";
        auto end_led = high_resolution_clock::now();
        std::chrono::duration<double> timer = end_led-start_led;
        std::cout << "LED subscriber runtime = " << timer.count() << "\n";
        std::ofstream outfile;
        outfile.open("piResultsCppMono.txt", std::ios_base::app); // append to the results text file
        outfile << "LED subscriber runtime = " << timer.count() << "\n";
        return 0;
    } else{
        if(document.HasMember("LED_1")) { // If the message is about the LED status, the LED is switch accordingly
            led_status = (bool) document["LED_1"].GetBool();
            pin_LED = document["GPIO"].GetInt();
            pinMode(pin_LED, OUTPUT);
            digitalWrite(pin_LED, led_status);
        }
        MQTTClient_freeMessage(&message);
        MQTTClient_free(topicName);
        return 1;
    }
}

void connlost(void *context, char *cause) // Required callback for lost connection
{
    printf("\nConnection lost\n");
    printf("     cause: %s\n", cause);
}

int publish_message(std::string str_message, const char *topic, MQTTClient client){
    // Initializing components for MQTT publisher
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;

    // Updating values of pubmsg object
    char *message = new char[str_message.length() + 1];
    strcpy(message, str_message.c_str());
    pubmsg.payload = message;
    pubmsg.payloadlen = (int)std::strlen(message);
    pubmsg.qos = QOS;
    pubmsg.retained = 0;

    MQTTClient_publishMessage(client, topic, &pubmsg, &token); // Publish the message
    int rc = MQTTClient_waitForCompletion(client, token, TIMEOUT);
    return rc;
}

std::string json_to_string(const rapidjson::Document& doc){
    //Serialize JSON to string for the message
    rapidjson::StringBuffer string_buffer;
    string_buffer.Clear();
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(string_buffer);
    doc.Accept(writer);
    return std::string(string_buffer.GetString());
}

// Reading of the dht11 is rather complex in C/C++. See this site that explains how readings are made: http://www.uugear.com/portfolio/dht11-humidity-temperature-sensor-module/
int* read_dht11_dat()
{
    uint8_t laststate	= HIGH;
    uint8_t counter		= 0;
    uint8_t j		= 0, i;

    dht11_dat[0] = dht11_dat[1] = dht11_dat[2] = dht11_dat[3] = dht11_dat[4] = 0;

    // pull pin down for 18 milliseconds. This is called “Start Signal” and it is to ensure DHT11 has detected the signal from MCU.
    pinMode( DHTPIN, OUTPUT );
    digitalWrite( DHTPIN, LOW );
    delay( 18 );
    // Then MCU will pull up DATA pin for 40us to wait for DHT11’s response.
    digitalWrite( DHTPIN, HIGH );
    delayMicroseconds( 40 );
    // Prepare to read the pin
    pinMode( DHTPIN, INPUT );

    // Detect change and read data
    for ( i = 0; i < MAXTIMINGS; i++ )
    {
        counter = 0;
        while ( digitalRead( DHTPIN ) == laststate )
        {
            counter++;
            delayMicroseconds( 1 );
            if ( counter == 255 )
            {
                break;
            }
        }
        laststate = digitalRead( DHTPIN );

        if ( counter == 255 )
            break;

        // Ignore first 3 transitions
        if ( (i >= 4) && (i % 2 == 0) )
        {
            // Add each bit into the storage bytes
            dht11_dat[j / 8] <<= 1;
            if ( counter > 16 )
                dht11_dat[j / 8] |= 1;
            j++;
        }
    }

    // Check that 40 bits (8bit x 5 ) were read + verify checksum in the last byte
    if ( (j >= 40) && (dht11_dat[4] == ( (dht11_dat[0] + dht11_dat[1] + dht11_dat[2] + dht11_dat[3]) & 0xFF) ) )
    {
        return dht11_dat; // If all ok, return pointer to the data array
    } else  {
        dht11_dat[0] = -1;
        return dht11_dat; //If there was an error, set first array element to -1 as flag to main function
    }
}

int main(int argc, char* argv[])
{

    std::string input = argv[1]; // IP address as command line argument to avoid hard coding
    input.append(":1883"); // Append MQTT port
    char char_input[input.length() + 1];
    strcpy(char_input, input.c_str());
    ADDRESS = char_input;
    int rc;

    wiringPiSetup(); // Required for wiringPi

    // ------ PIR code ----- //

    auto start_pir = high_resolution_clock::now(); // Starting timer

    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_create(&client, ADDRESS, CLIENTID_PIR, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS){
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    } else{
        printf("Connected to PIR. Result code %d\n", rc);
    }

    pinMode(PIN_PIR, INPUT);
    bool motion = false;
    int count = 0;
    while(count <= 100) {
        if(count == 100){
            rapidjson::Document document_done;
            document_done.SetObject();
            rapidjson::Document::AllocatorType& allocator1 = document_done.GetAllocator();
            document_done.AddMember("Done", true, allocator1);
            std::string pub_message_done = json_to_string(document_done);
            rc = publish_message(pub_message_done, TOPIC_PIR, client);
        }
        else {
            motion = digitalRead(PIN_PIR);
            //Create JSON DOM document object for humidity
            rapidjson::Document document_pir;
            document_pir.SetObject();
            rapidjson::Document::AllocatorType &allocator2 = document_pir.GetAllocator();
            document_pir.AddMember("PIR", motion, allocator2);
            try {
                std::string pub_message_pir = json_to_string(document_pir);
                rc = publish_message(pub_message_pir, TOPIC_PIR, client);
            } catch (const std::exception &exc) {
                // catch anything thrown within try block that derives from std::exception
                std::cerr << exc.what();
            }
        }
        count = count + 1;
    }

    // End of PIR loop. Calculate runtime
    auto end_pir = high_resolution_clock::now();
    std::chrono::duration<double> timer_pir = end_pir-start_pir;
    std::ofstream outfile;
    outfile.open("piResultsCppMono.txt", std::ios_base::app); // append to the results text file
    outfile << "PIR publisher runtime = " << timer_pir.count() << "\n";
    std::cout << "PIR runtime = " << timer_pir.count() << "\n";

    // ------ Humidity temperature code ------ //

    auto start_HT = high_resolution_clock::now(); // Starting timer

//    MQTTClient client;
//
//    MQTTClient_create(&client, ADDRESS, CLIENTID_HT, MQTTCLIENT_PERSISTENCE_NONE, NULL);
//
//    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
//    {
//        printf("Failed to connect, return code %d\n", rc);
//        exit(EXIT_FAILURE);
//    } else{
//        printf("Connected to humidity and temperature. Result code %d\n", rc);
//    }

    double temperature = 0;
    double humidity = 0;
    int *readings = read_dht11_dat();
    int counter = 0;
    while(readings[0] == -1 && counter < 50){
        readings = read_dht11_dat(); // Errors frequently occur when reading dht sensor. Keep reading until values are returned.
    }
    if(counter == 5){
        std::cout << "Problem with DHT11 sensor. Check Raspberry Pi \n";
        return 1;
    }
    humidity = readings[0] + (readings[1]/10);
    temperature = readings[2] + (readings[3]/10);

    count = 0;
    while(count <= 100) {
        if(count == 100){
            rapidjson::Document document_done;
            document_done.SetObject();
            rapidjson::Document::AllocatorType& allocator1 = document_done.GetAllocator();
            document_done.AddMember("Done", true, allocator1);
            std::string pub_message_done = json_to_string(document_done);
            rc = publish_message(pub_message_done, TOPIC_T, client);
            rc = publish_message(pub_message_done, TOPIC_H, client);
        }
        else {
            //Create JSON DOM document object for humidity
            rapidjson::Document document_humidity;
            document_humidity.SetObject();
            rapidjson::Document::AllocatorType &allocator2 = document_humidity.GetAllocator();
            document_humidity.AddMember("Humidity", humidity, allocator2);
            document_humidity.AddMember("Unit", "%", allocator2);

            //Create JSON DOM document object for temperature
            rapidjson::Document document_temperature;
            document_temperature.SetObject();
            rapidjson::Document::AllocatorType &allocator3 = document_temperature.GetAllocator();
            document_temperature.AddMember("Temp", temperature, allocator3);
            document_temperature.AddMember("Unit", "C", allocator3);
            try {
                std::string pub_message_humidity = json_to_string(document_humidity);
                rc = publish_message(pub_message_humidity, TOPIC_H, client);
                std::string pub_message_temperature = json_to_string(document_temperature);
                rc = publish_message(pub_message_temperature, TOPIC_T, client);
            } catch (const std::exception &exc) {
                // catch anything thrown within try block that derives from std::exception
                std::cerr << exc.what();
            }
        }
        count = count + 1;
    }

    // End of loop. Calculate runtime
    auto end_HT = high_resolution_clock::now();
    std::chrono::duration<double> timer_HT = end_HT-start_HT;
    std::ofstream outfile2;
    outfile2.open("piResultsCppMono.txt", std::ios_base::app); // append to the results text file
    outfile2 << "Humidity and temperature publisher runtime = " << timer_HT.count() << "\n";
    std::cout << "Humidity and temperature runtime = " << timer_HT.count() << "\n";

    // ------ LED code ------ //

    wiringPiSetup();

    MQTTClient client_led;
    MQTTClient_create(&client_led, ADDRESS, CLIENTID_LED, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    MQTTClient_setCallbacks(client_led, NULL, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client_led, &conn_opts)) != MQTTCLIENT_SUCCESS) //Unsuccessful connection
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    else{ // Successful connection
        printf("Connected to led. Result code %d\n", rc);
    }
    MQTTClient_subscribe(client_led, TOPIC_LED, QOS);

    while(session_status != "Done"){ // Continue listening for messages until end of session
        //Do nothing
    }

    //MQTTClient_unsubscribe(client, TOPIC);
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    MQTTClient_disconnect(client_led, 10000);
    MQTTClient_destroy(&client_led);
//    MQTTClient_disconnect(client_ht, 10000);
//    MQTTClient_destroy(&client_ht);
    digitalWrite(pin_LED, 0);

    return rc;
}