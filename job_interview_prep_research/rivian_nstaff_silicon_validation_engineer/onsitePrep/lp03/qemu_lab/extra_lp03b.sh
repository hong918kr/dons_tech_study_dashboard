#!/bin/sh
export PATH=/usr/bin:/bin:/sbin:/usr/sbin
echo "### rp1 -> Gen1"; sh /retrain.sh 00:01.0 1
echo "### rp1 dry-run Gen4"; sh /retrain.sh 00:01.0 4 --dry-run
echo "### rp1 Gen5 (above cap)"; sh /retrain.sh 00:01.0 5
echo "### switch DSP 03:00.0 Gen1"; sh /retrain.sh 03:00.0 1
echo "### endpoint 01:00.0 (must refuse)"; sh /retrain.sh 01:00.0 1
echo "### empty rp3 00:03.0 Gen1"; sh /retrain.sh 00:03.0 1
