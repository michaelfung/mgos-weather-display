#!/bin/bash

# Use this shell script to launch the "get-calendar-events.pl" script

# setup perlbrew environment
export PERLBREW_ROOT=/opt/perlbrew
source /opt/perlbrew/etc/bashrc
perlbrew use 5.22.0-1.001

# get sensitive info
source /home/mike/GOOGLE_ENV.sh

cd /home/mike/mgos-weather-display/scripts/calendar-events
exec ./get-calendar-events.pl
