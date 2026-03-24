import os
import math

from lab.environments import SlurmEnvironment, is_build_step, is_run_step
from lab import tools
from pathlib import Path

class SnelliusEnvironment(SlurmEnvironment):

    DEFAULT_MEMORY_PER_CPU="3500M"
    MAX_TASKS=1000
    PARALLEL_RUNS_PER_TASK=12
    DEFAULT_QOS = "normal"
    DEFAULT_PARTITION = "genoa"

    RUN_JOB_BODY_TEMPLATE_FILE="../../../../../../snellius-run-job-body"
    JOB_HEADER_TEMPLATE_FILE="../../../../../../snellius-run-job-header"
    DEFAULT_TIME_LIMIT_PER_TASK="02:30:00"

    #def run_steps(self, steps):
    #    print("Hello This is Snellius")


    def __init__(self,**kwargs,):
        super().__init__(**kwargs)
        self.cpus_per_task = self.PARALLEL_RUNS_PER_TASK


    def _get_num_runs_per_task(self):
        #print(f"GETNUM {len(self.exp.runs)} {self.MAX_TASKS}")
        num_runs = len(self.exp.runs)
        num_run_clusters = math.ceil(num_runs / self.cpus_per_task)
        #print(f"GETNUM: {num_runs} -> {num_run_clusters} ")
        return math.ceil(num_run_clusters / self.MAX_TASKS) * self.cpus_per_task


    def run_steps(self, steps):
        """
        We can't submit jobs from within the grid, so we submit them
        all at once with dependencies. We also can't rewrite the job
        files after they have been submitted.
        """
        self.exp.build(write_to_disk=False)

        # Prepare job dir.
        self.job_dir = Path(self.exp.path + "-grid-steps")
        if os.path.exists(self.job_dir):
            tools.confirm_or_abort(
                f'The path "{self.job_dir}" already exists, so the experiment has '
                f"already been submitted. Are you sure you want to "
                f"delete the grid-steps and submit it again?"
            )
            tools.remove_path(self.job_dir)
        
        # Overwrite exp dir if it exists.
        if any(is_build_step(step) for step in steps):
            self.exp._remove_experiment_dir()

        # Remove eval dir if it exists.
        if os.path.exists(self.exp.eval_dir):
            tools.confirm_or_abort(
                f'The evaluation directory "{self.exp.eval_dir}" already exists. '
                f"Do you want to remove it?"
            )
            tools.remove_path(self.exp.eval_dir)

        # Create job dir only when we need it.
        tools.makedirs(self.job_dir)

        prev_job_id = None
        for step in steps:
            print(f"Step {step}")
            job_name = self._get_job_name(step)
            job_file = self.job_dir / job_name
            job_content = self._get_job(step, is_last=(step == steps[-1])).replace("PARALLEL_RUNS_PER_TASK",str(self.PARALLEL_RUNS_PER_TASK))
            print(f"File {job_file}")
            tools.write_file(job_file, job_content)
            prev_job_id = self._submit_job(job_file, dependency=prev_job_id)


    #def _submit_job(self, job_name, job_file, job_dir, dependency=None):
    #    print(f"Would be submitting: {job_name} {job_file} {job_dir}")


