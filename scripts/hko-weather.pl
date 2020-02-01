#!/usr/bin/env perl

# read weather report from HKO
# More Info:
#   https://data.gov.hk/tc-data/dataset/hk-hko-rss-current-weather-report

require v5.22;

use Modern::Perl '2015';
use POSIX;
use Mojo::UserAgent;
use Net::MQTT::Simple;
use Sys::Syslog qw(:standard :macros);

openlog('hko-weather', 'ndelay', LOG_USER);

my $mqtt = Net::MQTT::Simple->new("hab2.lan");
my $hko_url = 'https://data.weather.gov.hk/weatherAPI/opendata/weather.php?dataType=rhrread&lang=en';
my $place = 'Tsuen Wan Shing Mun Valley';
my $current_temp;
my $current_humid;
my $found = 0;
my $ua  = Mojo::UserAgent->new;
$ua->connect_timeout(30)->request_timeout(60);
$ua->on(error => sub {
        my ($ua, $err) = @_;
        syslog("error", "HTTP client error: " . $err);
});
my $tries = 3;

while ($tries) {
    $tries--;
    my $res = $ua->get($hko_url)->result;

    if ($res->is_success)  {
        # extract target place temp:
        my $json = $res->json;

        unless ($json) {
            syslog("error", "invalid json data");
            next;
        }

        # post humidity
        $current_humid = $json->{humidity}{data}[0]{value};        
        my $humid_str = ($current_humid) ? sprintf("%3d", floor($current_humid)) : 'ERR';
        $mqtt->retain( "weather/hko/hk/humid" => "$humid_str");
        syslog("info", "Humidity: " .  $current_humid);

        for my $data (@{$json->{temperature}{data}}) {
            if ($data->{place} eq $place) {
                $current_temp = $data->{value};
                syslog("info", "Temperature: " .  $current_temp);
                $found = 1;
            }
        }

        unless ($found) {
            syslog("error", "Error: data not found for location");
        }
    }

    elsif ($res->is_error) {
        syslog("error", "Error message: " . $res->message);
    }

    else {
        syslog("error", "Error: HTTP Code: " . $res->code);
    }

    # success, post to MQTT
    if ($found) {
        # format to string of integer
        my $temp_str = sprintf("%3d", floor($current_temp));
        $mqtt->retain( "weather/hko/tsuenwan/temp" => "$temp_str");
    }
}

exit;

=for comment

Sample response:

{
    "rainfall":{
        "data":[
            {
                "unit":"mm",
                "place":"Central & Western District",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Eastern District",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Kwai Tsing",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Islands District",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"North District",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Sai Kung",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Sha Tin",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Southern District",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Tai Po",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Tsuen Wan",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Tuen Mun",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Wan Chai",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Yuen Long",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Yau Tsim Mong",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Sham Shui Po",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Kowloon City",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Wong Tai Sin",
                "max":0,
                "main":"FALSE"
            },
            {
                "unit":"mm",
                "place":"Kwun Tong",
                "max":0,
                "main":"FALSE"
            }
        ],
        "startTime":"2019-12-11T09:45:00+08:00",
        "endTime":"2019-12-11T10:45:00+08:00"
    },
    "icon":[
        50
    ],
    "iconUpdateTime":"2019-12-11T10:00:00+08:00",
    "uvindex":{
        "data":[
            {
                "place":"King's Park",
                "value":3,
                "desc":"moderate"
            }
        ],
        "recordDesc":"During the past hour"
    },
    "updateTime":"2019-12-11T11:02:00+08:00",
    "temperature":{
        "data":[
            {
                "place":"King's Park",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Hong Kong Observatory",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Wong Chuk Hang",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Ta Kwu Ling",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Lau Fau Shan",
                "value":19,
                "unit":"C"
            },
            {
                "place":"Tai Po",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Sha Tin",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Tuen Mun",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Tseung Kwan O",
                "value":23,
                "unit":"C"
            },
            {
                "place":"Sai Kung",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Cheung Chau",
                "value":23,
                "unit":"C"
            },
            {
                "place":"Chek Lap Kok",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Tsing Yi",
                "value":20,
                "unit":"C"
            },
            {
                "place":"Shek Kong",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Tsuen Wan Ho Koon",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Tsuen Wan Shing Mun Valley",
                "value":24,
                "unit":"C"
            },
            {
                "place":"Hong Kong Park",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Shau Kei Wan",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Kowloon City",
                "value":24,
                "unit":"C"
            },
            {
                "place":"Happy Valley",
                "value":24,
                "unit":"C"
            },
            {
                "place":"Wong Tai Sin",
                "value":23,
                "unit":"C"
            },
            {
                "place":"Stanley",
                "value":22,
                "unit":"C"
            },
            {
                "place":"Kwun Tong",
                "value":23,
                "unit":"C"
            },
            {
                "place":"Sham Shui Po",
                "value":24,
                "unit":"C"
            },
            {
                "place":"Kai Tak Runway Park",
                "value":23,
                "unit":"C"
            },
            {
                "place":"Yuen Long Park",
                "value":21,
                "unit":"C"
            },
            {
                "place":"Tai Mei Tuk",
                "value":22,
                "unit":"C"
            }
        ],
        "recordTime":"2019-12-11T11:00:00+08:00"
    },
    "warningMessage":[
        "The Fire Danger Warning is Red and the fire risk is extreme."
    ],
    "mintempFrom00To09":"",
    "rainfallFrom00To12":"",
    "rainfallLastMonth":"",
    "rainfallJanuaryToLastMonth":"",
    "tcmessage":"",
    "humidity":{
        "recordTime":"2019-12-11T11:00:00+08:00",
        "data":[
            {
                "unit":"percent",
                "value":52,
                "place":"Hong Kong Observatory"
            }
        ]
    }
}

=cut
