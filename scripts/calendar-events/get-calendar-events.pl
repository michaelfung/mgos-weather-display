#!/usr/bin/env perl

use Modern::Perl;
use POSIX;
use IO::All;
use Mojo::UserAgent;
use Mojo::JSON qw();
use JSON::XS qw(decode_json encode_json);
use Data::Dumper;
use Crypt::PK::RSA;
use Crypt::JWT qw(encode_jwt decode_jwt);
use DateTime;
use DateTime::Format::ISO8601;
use Sys::Syslog qw(:standard :macros);

openlog('get-calendar-events', 'ndelay', LOG_USER);

# hardcode time zone to Hong Kong
$ENV{TZ} = 'Asia/Hong_Kong';
POSIX::tzset;

my $GOOGLE_CALENDAR_ID = undef;
unless ($GOOGLE_CALENDAR_ID = $ENV{GOOGLE_CALENDAR_ID}) {
	syslog("err", 'GOOGLE_CALENDAR_ID environment not set');
	exit 1;
}

my $access_token;
my $access_token_expire;

my $sa_json = io->file('./calapi-service-account.json')->binary->slurp;
my $sa_obj = decode_json($sa_json);

my $pk = Crypt::PK::RSA->new(\$sa_obj->{private_key}); # in PKCS#8 format

#warn Dumper $pk;

my $dt = DateTime->now;
my $time_min = $dt->ymd('-') . 'T' . $dt->hms(':') . 'Z';
my $now = time();
my $ua = Mojo::UserAgent->new();
$ua->connect_timeout(30)->request_timeout(60);
$ua->on(
	error => sub {
		my ($ua, $err) = @_;
		syslog("err", "HTTP client error: " . $err);
	}
);
my $wmatrix_url = 'http://wmatrix.lan/rpc';


sub refresh_token {
	my $result = undef;

	my $claim = {
		"iss" => $sa_obj->{client_email},
		"scope" => "https://www.googleapis.com/auth/calendar.readonly",
		"aud" => "https://accounts.google.com/o/oauth2/token",
		"iat" => $now,
		"exp" => $now + 3600,
	};

	my $jwt = encode_jwt(payload => $claim, alg => 'RS256', key => $pk);

	my $res = $ua->post(
		'https://accounts.google.com/o/oauth2/token'=> form => {
			grant_type => 'urn:ietf:params:oauth:grant-type:jwt-bearer',
			assertion => $jwt,
		}
	)->result;

	if ($res->is_success)  {

		#warn $res->body;

		if ($res->code eq '200') {
			$access_token = $res->json('/access_token');
			$access_token_expire = $now + $res->json('/expires_in');
			$result = 1;

			#say sprintf "refresh calendar api token OK. access token=%s, expires_in=%d", $access_token, $res->json('/expires_in');
			syslog("info", sprintf "refresh calendar api token OK. access token=[...%s], expires_in=%d", substr($access_token, -4), $res->json('/expires_in'));
		} else {
			syslog("err", sprintf "refresh calendar api token failed, http code=%s", $res->code);
		}

	}elsif ($res->is_error) {
		syslog("err", 'get token failed, error message: ' . $res->message);
	}else {
		syslog("err", 'get token failed, Unknown Error');
	}

	return $result;
}


sub set_reminder {
	my ($event) = @_;

	# test: curl -v http://wmatrix.lan/rpc -d '{"method":"SetReminder","params":{"reminder":"test curl rpc call"}}'
	my $res = $ua->post(
		$wmatrix_url=> json => {
			id => $event->{id},
			method => 'SetReminder',
			params => {
				reminder => $event->{summary}
			},
		}
	)->result;

	if ($res->is_success && ($res->code eq '200')) {
		syslog("info", sprintf "set_reminder OK. reminder: %s", $event->{summary} );
	} else {
		syslog("err", sprintf "set_reminder call failed, reminder: %s", $event->{summary} );
	}
}


# --- main entry ----
if (refresh_token()) {

	# spec:
	# GET https://www.googleapis.com/calendar/v3/calendars/<calendarId>/events
	my $url = 'https://www.googleapis.com/calendar/v3/calendars/' . $GOOGLE_CALENDAR_ID . '/events';
	my $res = $ua->get(
		$url=> {
			Authorization => 'Bearer ' . $access_token,
			'X-GFE-SSL' => 'yes',
		}=> form => {
			timeMin => $time_min,
			maxResults => 5,
			singleEvents => 'true',
			orderBy => 'startTime',
		}
	)->result;

	if ($res->is_success)  {

		#warn $res->body;
		my $next_event;
		if ($res->code eq '200') {
			unless ($next_event = $res->json->{items}[0]) {
				syslog("info", "no event, exit");
				exit 0;
			}

			# get start date/time of it
			# NOTE: hardcode to GMT+0800 for whole day event since it does not provide timezone
			my $start_time =
			  (exists $next_event->{start}{date})
			  ? DateTime::Format::ISO8601->parse_datetime( $next_event->{start}{date} . 'T00:00:00+08:00' )
			  : DateTime::Format::ISO8601->parse_datetime( $next_event->{start}{dateTime} );

			#warn $start_time->ymd('-') . 'T' . $start_time->hms(':');
			#warn $start_time->epoch();
			#warn $next_event->{summary};

			# reminder logic:
			# 12 hours beforehand = noon of the previous day
			my $rpc_result;
			if (exists $next_event->{start}{date}) {

				# for whole day event:
				if ($now >= ($start_time->epoch() - (12 * 3600))) {

					# call set reminder rpc
					set_reminder($next_event);
				}
			} else {

				# for non whole day event
				# first, change start time to the previous day
				my $dur = DateTime::Duration->new( days => 1);
				$start_time->subtract_duration($dur); # -1 day
				$start_time->truncate( to => 'day' );  # trim to 12:00AM
				# then check if noon reached
				if ($now >= ($start_time->epoch() + (12 * 3600))) {

					# call set reminder rpc
					set_reminder($next_event);
				}
			}
		}
	}
}
