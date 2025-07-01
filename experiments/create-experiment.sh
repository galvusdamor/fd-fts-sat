#!/bin/bash

python3 -m venv --prompt fd-fts-sat .venv
source .venv/bin/activate
pip install -U pip wheel
pip install lab

BASE=2025-06-27-bdd-omit-vars

for f in common_setup.py filters.py snellius-run-job-body.template snellius.py snellius-run-job-header.template ; do
	cp ../$BASE/$f .
done 

cp ../$BASE/$BASE.py $(basename ~+).py
