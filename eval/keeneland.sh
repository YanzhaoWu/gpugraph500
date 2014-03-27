#PBS -S /bin/bash
#PBS -N g500
#PBS -j oe
#PBS -o rtlog/run_$PBS_JOBID.log
#PBS -m a
#PBS -m e
#PBS -M matthias.hauck@stud.uni-heidelberg.de
#PBS -A UT-NTNL0208
#PBS -l walltime=00:30:00

cd $PBS_O_WORKDIR

mpirun --mca btl openib,self,sm -n ${p} ./../cpu_2d/g500 -s ${scale} -C ${srp} > result/p`printf "%04d" ${p}`_c`printf "%03d" ${srp}`_s`printf "%02d" ${scale}`.log
