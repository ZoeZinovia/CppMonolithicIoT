//
// Created by Shani du Plessis on 29/04/2021.
//

//
// Created by Shani du Plessis on 20/04/2021.
//

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


int main(int argc, char *argv[]){

    // Saving IP address for all programs
    std::string input = argv[1]; // IP address as command line argument to avoid hard coding
    input.append(":1883"); // Append MQTT port
    char char_input[input.length() + 1];
    strcpy(char_input, input.c_str());
    ADDRESS = char_input;

    wiringPiSetupGpio();

    // ------ LED code ------ //

    MQTTClient client_led;
    MQTTClient_connectOptions conn_opts_led = MQTTClient_connectOptions_initializer;
    int rc;
    MQTTClient_create(&client_led, ADDRESS, CLIENTID_LED, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts_led.keepAliveInterval = 20;
    conn_opts_led.cleansession = 1;

    MQTTClient_setCallbacks(client_led, NULL, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client_led, &conn_opts_led)) != MQTTCLIENT_SUCCESS) //Unsuccessful connection
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    else{ // Successful connection
        printf("Connected. Result code %d\n", rc);
    }
    MQTTClient_subscribe(client_led, TOPIC_LED, QOS);

    // ------ PIR code ----- //

    auto start_pir = high_resolution_clock::now(); // Starting timer

    MQTTClient client_pir;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_create(&client_pir, ADDRESS, CLIENTID_PIR, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    if ((rc = MQTTClient_connect(client_pir, &conn_opts)) != MQTTCLIENT_SUCCESS){
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    } else{
        printf("Connected. Result code %d\n", rc);
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
            rc = publish_message(pub_message_done, TOPIC_PIR, client_pir);
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
                rc = publish_message(pub_message_pir, TOPIC_PIR, client_pir);
            } catch (const std::exception &exc) {
                // catch anything thrown within try block that derives from std::exception
                std::cerr << exc.what();
            }
        }
        count = count + 1;
    }

    // End of PIR loop. Stop MQTT and calculate runtime
    auto end_pir = high_resolution_clock::now();
    std::chrono::duration<double> timer = end_pir-start_pir;
    std::ofstream outfile;
    outfile.open("piResultsCppMono.txt", std::ios_base::app); // append to the results text file
    outfile << "PIR publisher runtime = " << timer.count() << "\n";
    std::cout << "PIR runtime = " << timer.count() << "\n";

    // ------ Temp and Humidity code ------ //



    while(session_status != "Done"){ // Continue listening for messages until end of session
        //Do nothing
    }

    // Close LED MQTT connection
    //MQTTClient_unsubscribe(client, TOPIC);
    MQTTClient_disconnect(client_pir, 10000);
    MQTTClient_destroy(&client_pir);
    MQTTClient_disconnect(client_led, 10000);
    MQTTClient_destroy(&client_led);
    digitalWrite(pin_LED, 0);
    return rc;
}