#PBS -S /bin/bash
#PBS -N g500
#PBS -j oe
#PBS -o test.$PBS_JOBID.log
#PBS -m a
#PBS -m e
#PBS -M matthias.hauck@stud.uni-heidelberg.de
#PBS -A UT-NTNL0208
#PBS -l walltime=00:30:00
#PBS -l nodes=4:ppn=1

cd $PBS_O_WORKDIR

mpirun --mca btl openib,self,sm -n 4 ./../cpu_2d/g500 -s 21 -C 2
