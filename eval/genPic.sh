#!/bin/bash

datpath="./summary"

scale_dats=`find $datpath -name 'scale_*.dat' | sort`

printf "Found *.dat:\n"
printf "${scale_dats}\n"

if [ -f "$gppath" ];
then
	mkdir $gppath
fi
datpathe="${datpath//\\/\\\\}"

for sdat in $scale_dats
do

sfactor=`echo $sdat| sed 's/.*scale_O*\([1-9][0-9]*\).dat/\1/'` 

sed  -e "s#\$SCALE#${sfactor}#" -e "s#\$datpath#$datpathe#" < scale_phases.tgp > "rs_phases_s${sfactor}.gp" 
sed  -e "s#\$SCALE#${sfactor}#" -e "s#\$datpath#$datpathe#" < scale_activities.tgp > "rs_activities_s${sfactor}.gp" 

done

sf=`echo "${scale_dats}" | sed 's/.*scale_O*\([1-9][0-9]*\).dat*/\1/'| tr '\n' ' ' `

sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_node_scale.tgp   > teps_node_scale.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_per_node.tgp	 > teps_per_node.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_per_exp.tgp		 > teps_per_exp.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_per_exp_node.tgp > teps_per_exp_node.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_per_exp_snode.tgp > teps_per_exp_snode.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < teps_comp_snode.tgp > teps_comp_snode.gp
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < validation.tgp		 > validation.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < generation.tgp		 > generation.gp 
sed -e "s#\$scales#$sf#" -e "s#\$datpath#$datpathe#" < construction.tgp		 > construction.gp 

for plot in `ls *.gp`;
do
	gnuplot $plot
done
rm *.gp
