
## Setting up the benchmarks
Clone the benchmarks from ``https://github.com/aibasel/downward-benchmarks``
Then set the directory in your ``.bashrc`` via ``export DOWNWARD_BENCHMARKS=/PATH/TO/BENCHMARKS``


Also clone and compile ``git@github.com:KCL-Planning/VAL.git``. Then rename ``Validate`` to ``validate`` and add the directory where the executable sits to your path, e.g. using ``export PATH="${PATH}:/PATH/TO/BIN"``.

Make sure that you re-start your bash so that these definitions load correctly.

## Set up the virtual environment
Set up your python virtual environment

```
python3 -m venv --prompt fd-fts-sat .venv
source .venv/bin/activate
pip install -U pip wheel
pip install lab
```

## Activate
Whenever a new shell gets started, you need to re-activate the python virtual environment with this command:
```
source .venv/bin/activate
````

If the virtual environment is active, you have ``(fd-fts-sat)`` at the start of your shell line.

## Prepare the run script
Copy the last run script to a new file. There should be one run script for every sub-experiment that you ran.
Modify it accordingly so that the right encodings/searches/transformations are enabled.


Selecting the right ones is a bit finnicky. We can start at most 1000 jobs on snellius at once. Each job has 12 (with 3.5 GB memory limit) or 24 (with 1.5 GB memory limit) CPUs in parallel. The default is 12. If you want to change this, you need to modify the ``PARALLEL_RUNS_PER_TASK`` in ``snellius.py``.

Due to this restriction, you need to choose the time-limit high enough: Calculate how many jobs maybe executed in sequence (which is total number of runs divided by [1000 * number of CPUs available]). The timelimit ``DEFAULT_TIME_LIMIT_PER_TASK`` set in ``snellius.py`` needs to be at least 35 minutes times this number.


Now you need to run the first step of lab. If this is the first time you compile this configuration of fast-downward, this needs to happen on the cluster. To do so run:
``srun --time=30:00 --pty /bin/bash``
Check that the venv is active!
If you already compiled this version of fast downward, you can run this step on the login-node.

To create the experiment run
```python 202*-**-**-your-experiment.py 1``
This will generate the experiments directories. 

Now start the experiments with
```python 202*-**-**-your-experiment.py 2`` and wait.


## Checking after the run
After the experiments have run on Snellius, we need to check whether all runs were started.
For this navigate to the ``data/*/`` directory for this experiment. Then run
```find . | grep driver.log | wc -l```
To determine how many driver.log files are there. This must be identical to the number of runs that ought to be started.
If this is not the case (i.e. there are fewer driver.log files than expected), Snellius did not start some of the runs. In this case, you need to run the start step of lab again -- it will only re-run the experiments that did not start correctly. After they have finished, check again. This step might need to be repeated several times until all data is available.

Next, we need to check whether the runs also terminated cleanly. For this, you first need to run the parse and fetch step and then move to the ``data/*-eval/`` directory.
In it run
```grep "planner exit code" properties | cut -d/ -f 2- | sed 's/^/\//g ; s/",//g'```
This shows all runs for which no exit code is known. This typicall means that the planner instance was killed for some reason by the cluster without any reporting (we don't know why and when this happens). The affected runs need to be repeated.

If this is not empty, run  
```rm $(grep "planner exit code" properties | cut -d/ -f 2- | sed 's/^/\//g ; s/",//g')```
to delete all drive.log files that don't contain clean data and restart the runs with the same procedure as for the 


## Parsing
Now run the parser
```python 202*-**-**-your-experiment.py 3``
