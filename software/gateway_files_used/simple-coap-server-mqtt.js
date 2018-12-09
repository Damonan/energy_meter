var coap        = require('coap')
var server      = coap.createServer()
var mqtt        = require('mqtt');

var MQTT_TOPIC_NAME = 'coap-data';
var prev_data = -1;

var mqtt_client = mqtt.connect('mqtt://localhost');
mqtt_client.subscribe(MQTT_TOPIC_NAME);

mqtt_client.on('connect', function () {
    //debug('Connected to MQTT');
    console.log("connected to mqtt");
    // Start the gateway
    //CoapGateway.start();

    // the default CoAP port is 5683
    server.listen(function() {
      console.log("Listening")
    })


    server.on('request', function(req, res) {
      try {
        console.log('got request')
        var url = req.url;
        if (url === "/test") {
          console.log("got data: " + req.payload);
          ////Timestamp will need more parsing
          ////var sent_time = req.payload.slice(0, 4); // will need to uncomment
          var data = req.payload.readInt32BE(); // will need to uncomment, 32 bit little endian
          
          if (data != prev_data) {
            prev_data = data;
            var received_time = new Date();  
            var adv_obj = {
              count: data,

            };
            adv_obj._meta = {
              timestamp: received_time.getTime(),
              device_id: '0',
            };
          ////should we give the time, or add any other info to payload?
            mqtt_client.publish(MQTT_TOPIC_NAME, JSON.stringify(adv_obj));//previously stringify req.payload
            console.log("mqtt publish: " + adv_obj['count']); //previously + JSON.stringify(req.payload)
          }
        }
        res.end('Hello ' + req.url.split('/')[1] + '\n')
      } catch(e) {
        console.log(e);
      }
    })

});
