.. _thermal_zones:

===============
thermal_zones
===============

-------------------------------------------------------------------
An LDMS sampler plugin that monitors Linux thermal zone temperatures
-------------------------------------------------------------------

:Date:   November 2026
:Manual section: 7
:Manual group: LDMS sampler


SYNOPSIS
========

| Within ldmsd_controller or a configuration file:
| load name=thermal_zones config name=thermal_zones producer=<name>
  instance=<name> component_id=<int>

DESCRIPTION
===========

The thermal_zones sampler collects temperatures from the Linux thermal
zone interface at /sys/class/thermal. Entries named thermal_zone<N> in
this directory are enumerated, and each zone is represented by a record
in the zone_list list metric containing:

- zone_no: the zone number N
- type: the content of thermal_zone<N>/type (e.g. acpitz)
- temp: the zone temperature in Celsius (thermal_zone<N>/temp)
- trip_temp: the highest trip point temperature in Celsius
  (thermal_zone<N>/trip_point_*_temp), or -1 if the zone has none

This is a portable alternative to the coretemp sampler on platforms
that do not use the Intel coretemp driver (e.g. AMD or ARM systems).

Zones are discovered once, when the config action runs. Zones that
appear or disappear afterwards are not detected until the sampler is
re-configured.

CONFIGURATION ATTRIBUTE SYNTAX
==============================

See man base.

EXAMPLES
========

Within ldmsd_controller or a configuration file:

::

   load name=thermal_zones
   config name=thermal_zones producer=vm1_1 instance=vm1_1/thermal_zones
   start name=thermal_zones interval=1000000 offset=0

SEE ALSO
========

:ref:`ldmsd(8) <ldmsd>`, :ref:`ldms_quickstart(7) <ldms_quickstart>`, :ref:`ldmsd_controller(8) <ldmsd_controller>`, :ref:`ldms_sampler_base(7) <ldms_sampler_base>`,
:ref:`coretemp(7) <coretemp>`
