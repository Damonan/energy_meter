var async = require('async');
var noble = require('noble');
var InfluxPoster = require('influx-poster');


// Address of the NRF-Dev-Board acting as a slave
var slave_address = 'c0:98:e5:49:00:03';

// How long to batch data for before posting
var DATA_LIMIT_LINES = 200000;
var DATA_LIMIT_TIME  = 15*1000;


//change
var host = '35.171.166.120';
var username = 'eecs149proj';
var password = 'energy123!';
var database = 'energyusageBLE';
var port = 8086;
var protocol = 'http';

//connect to host (change http address)
// var client = new influx({host: '35.171.166.120', port: 8086, protocol: 'http', username: 'eecs149proj', password: 'energy123!', database: 'energyusageBLE'});


//used https://github.com/lab11/gateway/blob/master/software/ble-influxdb/ble-influxdb.js as example for influxdb
var influx_poster = new InfluxPoster({
  host: host,
  database: database,
  port: port,
  protocol: protocol,
  username: username,
  password: password,
}, DATA_LIMIT_LINES, DATA_LIMIT_TIME);

console.log("Using influx at " + protocol + "://" + host + ":" + port + "  db=" + database)

function fix_measurement (field) {

  function fix_measurement_no_objects (subfield) {
    if (typeof subfield === 'object') {
      return JSON.stringify(field);
    } else if (subfield === null) {
      return 'null';
    } else if (typeof subfield === 'number') {
      return subfield;
    } else if (typeof subfield === 'boolean') {
      return subfield;
    } else if (typeof subfield === 'string') {
      if (field.lower() === 'true') {
        return true;
      } else if (field.lower() === 'false') {
        return false;
      } else if (isFloat(field)) {
        parseFloat(field);
      } else {
        return field;
      }
    } else {
      return JSON.stringify(field);
    }
  }

  // Taken from https://github.com/chriso/validator.js/blob/master/lib/isFloat.js
  function isFloat (str) {
    var float = /^(?:[-+]?(?:[0-9]+))?(?:\.[0-9]*)?(?:[eE][\+\-]?(?:[0-9]+))?$/;

    if (str === '' || str === '.') {
      return false;
    }
    return float.test(str);
  }

  if (Array.isArray(field)) {
    // We cannot pass an array to Influx, so we must make it a string
    // before sending it to Influx.
    return JSON.stringify(field);
  } else if (field === null) {
    // There is no "null" type in Influx, not really sure what the user
    // wants, so lets send a string. Seems better than forcing it to a
    // bool.
    return 'null';
  } else if (typeof field === 'object') {
    // Want to pass this as a complex measurement. Otherwise we would
    // try to store "[object object]".
    var out = {};
    for (var key in field) {
        out[key] = fix_measurement_no_objects(field[key]);
    }
    return out;
  } else if (typeof field === 'number') {
    // A number will get stored as a float.
    return {value: field};
  } else if (typeof field === 'boolean') {
    // Booleans are OK too.
    return {value: field};
  } else if (typeof field === 'string') {
    // Strings are fine, but we want to promote things which are obviously
    // bools or numbers to the proper type.
    if (field.toLowerCase() === 'true') {
      // Check for any of 'true', 'True', 'TRUE', etc.
      return {value: true};
    } else if (field.toLowerCase() === 'false') {
      return {value: false};
    } else if (isFloat(field)) {
      // If this looks like a valid number, make it an actual number.
      // Since JS doesn't really do integers, and the influx publishing
      // library doesn't use the integer data type, no need to bother
      // worrying about if the number is an integer or not.
      return {value: parseFloat(field)};
    } else {
      // Well, guess it's just a string!
      return {value: field};
    }
  } else {
    // Based on the allowed types in a JSON, we should never get to
    // this case.
    console.log('Error parsing type (' + typeof field + ') of: ' + field);
    return {value: JSON.stringify(field)};
  }

}


// client.getDatabaseNames().then(names=>{
//   if(!names.include('EnergyUsage')){
//     client.createDatabase('EnergyUsage');
//   }
// });

console.log('Looking for Slave');

noble.on('stateChange', function(state) {
  if (state === 'poweredOn') {
    console.log("Starting scan...");
    noble.startScanning([], true);
  } else {
    console.log("Stopped scan");
    noble.stopScanning();
  }
});

noble.on('discover', function(peripheral) {
  //var advertisement = peripheral.advertisement;
  //var count = advertisement.manufacturerData.readUIntBE(3,4).toString();
  //var t = advertisement.manufacturerData.readUIntBE(7,4).toString();
  if (peripheral.address == slave_address) { 
    var advertisement = peripheral.advertisement;
    var count = advertisement.manufacturerData.readUIntBE(3,4).toString();
    var t = advertisement.manufacturerData.readUIntBE(7,4).toString();

    console.log('Received update from slave!');
    console.log('count: ', count);
    var now = new Date().toISOString();
    /*var adv_obj = {
        'device': 'BLEPacket',
        'ble_id': peripheral.id,
        'receive_time': now,
        'sequence_number': -1,
        'device_type': 'unknown',
        'rssi': peripheral.rssi,
    };
    adv_obj._meta = {
        received_time: now,
        device_id:     peripheral.id,
        receiver:      'ble-influxdb',
        gateway_id:    this_gateway_id,
    };
    var timestamp  = new Date(adv_obj['_meta']['received_time']).getTime();
    var receiver   = adv_obj['_meta']['receiver'];
    var gateway_id = adv_obj['_meta']['gateway_id'];
    for (var key in adv_obj) {
      var tags = {
        device_id: device_id,
        device_class: device_class,
        receiver: receiver,
        gateway_id: gateway_id,
      };

      var fields = fix_measurement(adv_obj[key]);

      var point = [
        key,
        tags,
        fields,
        timestamp
      ];
    
      influx_poster.write_data(point);
    }
    */
    influx_poster.write_data([
      {
        key: 'ble',
        tags: {},
        fields: { count_data: count },
        timestamp: now
      }
    ]);

  }
  //TODO Add code to store count data and timestamp in database
  // client.writePoint(info.series.name, {time: new Date(), value: count}, null, done).catch(err => {
  //     console.error(`Error saving data to InfluxDB! ${err.stack}`)
  //   });
     

});

