var async = require('async');
var noble = require('noble');
var Influx = require('influx');

// Address of the NRF-Dev-Board acting as a slave
var slave_address = 'c0:98:e5:49:00:03';

//connest to host (change http address)
const influx = new Influx.InfluxDB('http://user:password@host:8086/database');


influx.getDatabaseNames().then(names=>{
  if(!names.include('EnergyUsage')){
    influx.createDatabase('EnergyUsage');
  }
});

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
  var advertisement = peripheral.advertisement;
  var count = advertisment.manufacturerData.readUIntBE(3,4).toString();
  if (peripheral.address == slave_address) { 
    console.log('Received update from slave!');
    console.log('count: ', count);
  }
  //TODO Add code to store count data and timestamp in database
  const start = Date.now()
  influx.writePoints([
      {
        measurement: 'watts', //change this based on main database
        // tags: { host: os.hostname() },
        fields: { count }, //change to match main database
      }
    ]).catch(err => {
      console.error(`Error saving data to InfluxDB! ${err.stack}`)
    });
     

});

