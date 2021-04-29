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

int main(int argc, char *argv[]){

    // Saving IP address for all programs
    std::string input = argv[1]; // IP address as command line argument to avoid hard coding
    input.append(":1883"); // Append MQTT port
    char char_input[input.length() + 1];
    strcpy(char_input, input.c_str());
    ADDRESS = char_input;

    wiringPiSetupGpio();

    // MQTT setup for LED
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    int rc;

    MQTTClient_create(&client, ADDRESS, CLIENTID_LED, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, delivered);

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) //Unsuccessful connection
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(EXIT_FAILURE);
    }
    else{ // Successful connection
        printf("Connected. Result code %d\n", rc);
    }
    MQTTClient_subscribe(client, TOPIC_LED, QOS);



    while(session_status != "Done"){ // Continue listening for messages until end of session
        //Do nothing
    }

    // Close LED MQTT connection
    //MQTTClient_unsubscribe(client, TOPIC);
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    digitalWrite(pin_LED, 0);
    return rc;
}