


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
