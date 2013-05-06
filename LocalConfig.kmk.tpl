## @file
# Local Project Configuration File (Template)
#
# The local project configuration file is used to specify local paths to
# external tools and libraries and also to optioanlly override the global
# project configuration options.
#
# NOTES:
#
#   This file is a template! Copy it to a file named LocalConfig.kmk in
#   the same directory and modify the copy to fit your local environment.
#
#   All paths in this file are specified using forward slashes unless specified
#   otherwise.
#

#
# Section included at the top of Config.kmk.
# ------------------------------------------
#
ifdef LOCAL_CONFIG_PRE

#
# Base directory where all build output will go. The directory will be created
# if does not exist. The default is "out" in the root of the source tree.
#
# PATH_OUT_BASE := out

#
# Python executable, version 2.6 or above.
#
PYTHON := python

#
# Perl executable, version 5.x or above.
#
PERL := perl

#
# Section included at the bottom of Config.kmk.
# ---------------------------------------------
#
else # ifdef LOCAL_CONFIG_PRE

endif # ifdef LOCAL_CONFIG_PRE
