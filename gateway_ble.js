var async = require('async');
var noble = require('noble');

var slave_address = 'c0:98:e5:49:00:03';

var peripherals = {};

var aveTime = [10];
var timeIndex = 0;

var avePower = [10];
var aveApp = [10];
var aveIndex = 0;

var timesInThirty = [30];
var indexToThirty = 0;

var watt_hours_tot = 0;
var recv_last = 0;

var iden_count = 0;

console.log('Looking for ');

noble.on('stateChange', function(state) {
  if (state === 'poweredOn') {
    console.log("Starting scan...");
    noble.startScanning([], true);
  } else {
    noble.stopScanning();
  }
});

noble.on('discover', function(peripheral) {

  // device found!
  var advertisement = peripheral.advertisement;
  var manufacturer_id = 0;
  var service_id = 0;
  if (typeof advertisement.manufacturerData !== undefined && advertisement.manufacturerData && advertisement.manufacturerData.length >= 3) {
    manufacturer_id = advertisement.manufacturerData.readUIntLE(0,2);
    service_id = advertisement.manufacturerData.readUIntBE(2,1);
  }
  if (peripheral.address == slave_address) { 
    console.log('count: ', advertisement.manufacturerData.readUIntBE(3,1).toString());
  }
});

