var coap        = require('coap')
var server      = coap.createServer()
var mqtt        = require('mqtt');

var MQTT_TOPIC_NAME = 'coap-data';

//var MQTT_TOPIC_NAME_LOCAL = 'gateway-local';


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
        //console.log(req)
        var url = req.url;
        //console.log(url)
        if (url === "/test") {
          console.log("got data: " + req.payload);
          ////Timestamp will need more parsing
          ////var sent_time = req.payload.slice(0, 4); // will need to uncomment
          ////var data = req.payload.readInt32LE(4); // will need to uncomment, 32 bit little endian
          var data = req.payload;
          var sent_time = new Date();

          ////var timestamp = sent_time.getTime(); //might need .. = new Date(t_sec*1000 + t_usec/1000);
          ////timestamp = timestamp.toISOString(); // might not need this
          //
          var adv_obj = {
            count: 0, //data,
            //sent_time: sent_time,
            //timestamp: sent_time.toISOString(),

          };
          //
          adv_obj._meta = {
            timestamp: sent_time.getTime(), //sent_time.toISOString(),
            device_id: '0',
          };
          //
          //
          //
          //
          //
          //
          ////should we give the time, or add any other info to payload?
          mqtt_client.publish(MQTT_TOPIC_NAME, JSON.stringify(adv_obj));//previously stringify req.payload
          console.log("mqtt publish: " + adv_obj['count']); //previously + JSON.stringify(req.payload)

        }
        res.end('Hello ' + req.url.split('/')[1] + '\n')
      } catch(e) {
        console.log(e);
      }
    })



    // This is from MQTT
    // CoapGateway.on('payload', function (adv_obj) {
    //     debug(adv_obj);
    //     mqtt_client.publish(MQTT_TOPIC_NAME, JSON.stringify(adv_obj));

    //     // Also publish on /device
    //     GatewayTopics.publish(adv_obj);
    // });

    // CoapGateway.on('local', function (local_obj) {
    //     mqtt_client.publish(MQTT_TOPIC_NAME_LOCAL + '/' + local_obj._meta.device_id, JSON.stringify(local_obj), {retain: true});
    // });
});
/*
mqtt_client.on('message', function (topic, message) {
  if (topic == MQTT_TOPIC_NAME) {
            // message is Buffer
            var adv_obj = JSON.parse(message.toString());
            console.log("got mqtt: " + adv_obj['count']);

            var sent_time = new Date(adv_obj['sent_time']).getTime();
            var timestamp = adv_obj['timestamp'];*/
            /*// Get device id
            var device_id = undefined;
            if ('_meta' in adv_obj) {
                device_id = adv_obj._meta.device_id;
            } else if ('id' in adv_obj) {
                device_id = adv_obj.id;
            }

            // Make sure the device id is only alpha numerical characters
            device_id.replace(/\W/g, '');

            var device_class = adv_obj['device'];
            delete adv_obj.device;

            // pick out the more accurate timestamp if it exists
            var recv_time = new Date(adv_obj['_meta']['received_time']).getTime();
            var timestamp = recv_time;
            if ('timestamp' in adv_obj['_meta']) {
              timestamp = adv_obj['_meta']['timestamp'];
            }

            // Continue on to post to influxdb
            if (device_id) {

                // Add all keys that are in the _meta field to the
                // tags section of the stored packet.
                var tags = {};
                for (var key in adv_obj['_meta']) {
                    if (key != 'device_id' && key != 'received_time' && key != 'sent_time') {
                        tags[key] = adv_obj['_meta'][key];
                    }
                }

                tags.device_id = device_id;
                tags.device_class = device_class;

                // Delete meta key and possible id key
                delete adv_obj._meta;
                delete adv_obj.id;

                // Only publish if there is some data
                if (Object.keys(adv_obj).length > 0) {
                    for (var key in adv_obj) {
                        var fields = fix_measurement(adv_obj[key]);

                        var point = [
                            key,
                            tags,
                            fields,
                            timestamp
                        ];

                        influx_poster.write_data(point);
                    }
                }
            }*/
/*
  }
});
*/

