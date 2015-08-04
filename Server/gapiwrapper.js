var request = require('request');

var tokeninfo_url = "https://www.googleapis.com/oauth2/v2/tokeninfo?";

var google = require('googleapis');
var OAuth2Client = google.auth.OAuth2;
var plus = google.plus('v1');

var net = require('net');

function checkValid(tokens, CLIENT_ID, callback) {
	var url = tokeninfo_url+"access_token="+tokens.access_token;
	request.get(url, function(error, response, body) {
		if (!error && response.statusCode == 200) {
			var res = JSON.parse(body);
			if(res.issued_to == CLIENT_ID && res.audience == CLIENT_ID)
			{
				callback(res);
			}
		}else{
			console.error(error);
			console.log(response.statusCode);
			callback();
		}
	});
}

process.on('SIGINT', function(){
	unixServer.close();
	process.exit();
});

var unixServer = net.createServer(function(socket) {
	socket.on('error', function(err){
		console.error(err);
	});
	socket.on('end', function() {
	});
	socket.on('data', function(data) {
		var parsed = JSON.parse(data);
		var tokens = {access_token : parsed.access_token};
		var CLIENT_ID = parsed.client_id;
		var CLIENT_SECRET = parsed.client_secret;
		var REDIRECT_URL = parsed.redirect_url;
		checkValid(tokens, CLIENT_ID, function(response){
			if(typeof response === 'undefined')
			{
				socket.write("");
				socket.end("");
				return;
			}
			var oauth2Client = new OAuth2Client(CLIENT_ID, CLIENT_SECRET, REDIRECT_URL);
			oauth2Client.setCredentials(tokens);
			plus.people.get({ userId: 'me', auth: oauth2Client }, function(err, profile) {
				if (err) {
					socket.write("");
					socket.end("");
					return;
				}
				socket.write(JSON.stringify(response));
				socket.end(JSON.stringify(profile));
			});
		});
	});
})
.listen('/tmp/gapi.sock');