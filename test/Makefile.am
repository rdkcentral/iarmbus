check_PROGRAMS = test_reply_validation
test_reply_validation_SOURCES = test_reply_validation.c
test_reply_validation_CPPFLAGS = -I$(top_srcdir)/core
check_PROGRAMS += test_safec_stubs
test_safec_stubs_SOURCES = test_safec_stubs.c
test_safec_stubs_CPPFLAGS = -DSAFEC_DUMMY_API=1 -DIARM_ALLOW_DUMMY_SAFEC=1 -I$(top_srcdir)/stubs
TESTS = test_reply_validation test_safec_stubs test_member_name_validation.sh test_reply_wiring.sh test_rpc_length_contract.sh test_format_string.sh test_safec_configuration.sh
EXTRA_DIST = test_member_name_validation.sh test_reply_wiring.sh test_rpc_length_contract.sh test_format_string.sh test_safec_configuration.sh