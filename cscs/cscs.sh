#!/bin/bash

export IP1=148.187.83.175  # piccinal-ldms-test
export IP2=148.187.83.116  # piccinal-ldms-test2

# sampler
export l1='ldmsd -x sock:10001 -l logs/s1.log'
export lc1='ldmsd_controller -h localhost -x sock -p 10001'
export lc1_='ldmsctl -h localhost -x sock -p 10001'
export ls1='ldms_ls -h localhost -x sock -p 10001'

# aggregator
export l2='ldmsd -x sock:20001 -l logs/a.log'
export lc2='ldmsd_controller -h localhost -x sock -p 20001'
export lc2_='ldmsctl -h localhost -x sock -p 20001'
export ls2='ldms_ls -h localhost -x sock -p 20001'

# clean
export lk='pkill ldmsd'

# cd /opt/ ; sudo ln -s /shared/opt/ovis-ldms/ . ; cd -
. /opt/ovis-ldms/lib/ovis-ldms-configvars.sh

LDMSJG=$ovis_ldms_prefix
export PATH=$LDMSJG/bin:$LDMSJG/sbin:$PATH
export PYTHONPATH=$LDMSJG/lib/python3.9/site-packages:$PYTHONPATH
export ZAP_LIBPATH=$LDMSJG/lib/ovis-ldms:$ZAP_LIBPATH
export LD_LIBRARY_PATH=:$LDMSJG/lib:$LDMSJG/lib/ovis-ldms:$LD_LIBRARY_PATH

which ldmsd_controller
which ldms_ls
which ldmsctl
