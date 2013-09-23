$benchnum  = '421';
$benchname = 'ilbc';
$exename   = 'ilbc';
$benchlang = 'C';
@base_exe  = ($exename);

@sources=qw(
FrameClassify.c
LPCdecode.c
LPCencode.c
StateConstructW.c
StateSearchW.c
anaFilter.c
constants.c
createCB.c
doCPLC.c
enhancer.c
filter.c
gainquant.c
getCBvec.c
helpfun.c
hpInput.c
hpOutput.c
iCBConstruct.c
iCBSearch.c
iLBC_decode.c
iLBC_encode.c
iLBC_test.c
lsf.c
packing.c
syntFilter.c
);

$need_math='yes';

sub invoke {
    my ($me) = @_;
    my @rc;

    for ($me->input_files_base) {
        if (($name) = m/(.*).raw$/) {
            push (@rc, { 'command' => $me->exe_file,
                         'args'    => [ "20 $_ $name.20.bits $name.20.raw.out" ],
                         'output'  => "$name.20.out",
                         'error'   => "$name.20.err",
                        });
            push (@rc, { 'command' => $me->exe_file,
                         'args'    => [ "30 $_ $name.30.bits $name.30.raw.out" ],
                         'output'  => "$name.30.out",
                         'error'   => "$name.30.err",
                        });
        }
    }
    return @rc; 
}

1;    
