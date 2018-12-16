var coap		= require('coap')
var server		= coap.createServer()
var InfluxPoster	= require('influx-poster');
var util 		= require('util');

var prev_data = -1; 
//How long to batch data before posting, copied, maybe adjust
var DATA_LIMIT_LINES = 0; // 200000;
var DATA_LIMIT_TIME = 1*1000;

var influx_poster = new InfluxPoster({
  host: 'localhost', //not sure
  database: 'energyusageTHREAD',
  port: 8086,
  protocol: 'http',
  username: 'eecs149proj',
  password: 'energy123!',
  prefix: '',
}, DATA_LIMIT_LINES, DATA_LIMIT_TIME);

console.log("Using influx at http://localhost:8086 db=energyusageTHREAD")


server.on('request', function(req, res) {
	try {
		console.log('got request')
		var url = req.url;
		if (url === "/test") {
			console.log("got test message: " + req.payload);
                        ////var sent_time = req.payload.slice(0, 4); // will need to uncomment
                        ////var data = req.payload.readInt32LE(4); // will need to uncomment; 32bit, little endian
                        
                        var data = req.payload.readInt32BE(); // req.payload; 
                        if (data != prev_data) {
                          prev_data = data;
                          var sent_time = new Date();
                          var timestamp = sent_time.getTime(); // sent_time.toISOString(); 
                          var fields = {count2: data};
                          var key = 'count2';
                          var tags = {}; 
                          var point = [key, tags, fields, timestamp];
                          influx_poster.write_data(point);                        
                        } 
            


		}
		res.end('Hello ' + req.url.split('/')[1] + '\n')
	} catch(e) {
		console.log(e);
	}
})

// the default CoAP port is 5683
server.listen(null, function() {
	console.log("Listening")
})
