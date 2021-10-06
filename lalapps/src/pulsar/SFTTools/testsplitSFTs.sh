## run lalapps_Makefakedata_v5 to create fake broadband SFTs
Tsft=1800
start=1257800000
span=9000
cmdlinebase="lalapps_Makefakedata_v5 --IFOs H1 --sqrtSX 1e-24 --startTime ${start} --duration ${span} --fmin 10 --Band 85 --injectionSources '{Alpha=0.1; Delta=0.4; Freq=30; f1dot=1e-10; h0=1e-24; cosi=0.7; refTime=1257800000}'"
mkdir -p broadband1a/
cmdline="${cmdlinebase} --outSingleSFT=no --outSFTdir broadband1a/"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi
mkdir -p broadband1b/
cmdline="${cmdlinebase} --outSingleSFT=yes --outSFTdir broadband1b/"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi

## run lalapps_splitSFTs to create narrowband SFTs
mkdir -p narrowband1a/
cmdline="lalapps_splitSFTs -fs 10 -fe 95 -fb 8 -n narrowband1a/"
for sft in broadband1a/*.sft; do
    cmdline="${cmdline} $sft"
done
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi
mkdir -p narrowband1b/
cmdline="lalapps_splitSFTs -fs 10 -fe 95 -fb 8 -n narrowband1b/ -- broadband1b/H-5_H1_${Tsft}SFT_mfdv5-${start}-${span}.sft"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi

for i in 1a 1b; do

    ## check narrowband SFT names
    for freq in 10 18 26 34 42 50 58 66 74 82 90; do
        if [ $freq = 90 ]; then
            binwidth="0005Hz1"
        else
            binwidth="0008Hz0"
        fi
        sft="narrowband${i}/H-5_H1_${Tsft}SFT_NBF00${freq}Hz0W${binwidth}_mfdv5-${start}-${span}.sft"
        if ! test -f $sft; then
            echo "ERROR: could not find file '$sft'"
            exit 1
        fi
    done

    ## dump broadband and narrowband SFT bins, sorted by frequency
    for sftband in broadband narrowband; do
        cmdline="lalapps_dumpSFT -d -i '${sftband}${i}/*' | sed '/^$/d;/^%/d' | sort -n -k1,3 > ${sftband}${i}_bins.txt"
        if ! eval "$cmdline"; then
            echo "ERROR: something failed when running '$cmdline'"
            exit 1
        fi
    done

    ## broadband and narrowband SFT bins should be equal
    if ! diff -s broadband${i}_bins.txt narrowband${i}_bins.txt; then
        echo "ERROR: broadband${i}_bins.txt and narrowband${i}_bins.txt should be equal"
        exit 1
    fi

done

## split broadband1b/ set again, but with timestamps constraints

endTime1=$(echo "$start + $span" | bc)
startTime2=$(echo "$start + $Tsft" | bc)
endTime2=$(echo "$endTime1 - $Tsft" | bc)
span2=$(echo "$endTime2 - $startTime2" | bc)

## first using the constraint user options, but trying to get back the full split set
mkdir -p narrowband1c/
cmdline="lalapps_splitSFTs -fs 10 -fe 95 -fb 8 -n narrowband1c/ -ts ${start} -te ${endTime1} -- broadband1b/H-5_H1_${Tsft}SFT_mfdv5-${start}-${span}.sft"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi
## then actually reduce the range
mkdir -p narrowband1d/
cmdline="lalapps_splitSFTs -fs 10 -fe 95 -fb 8 -n narrowband1d/ -ts ${startTime2} -te ${endTime2} -- broadband1b/H-5_H1_${Tsft}SFT_mfdv5-${start}-${span}.sft"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi

## check narrowband1c/ SFT names (full range)
for freq in 10 18 26 34 42 50 58 66 74 82 90; do
    if [ $freq = 90 ]; then
        binwidth="0005Hz1"
    else
        binwidth="0008Hz0"
    fi
    sft="narrowband1c/H-5_H1_${Tsft}SFT_NBF00${freq}Hz0W${binwidth}_mfdv5-${start}-${span}.sft"
    if ! test -f $sft; then
        echo "ERROR: could not find file '$sft'"
        exit 1
    fi
done

## dump narrowband1c/ (full range) SFT bins, sorted by frequency, should match broadband1b/ from before
cmdline="lalapps_dumpSFT -d -i 'narrowband1c/*' | sed '/^$/d;/^%/d' | sort -n -k1,3 > narrowband1c_bins.txt"
if ! eval "$cmdline"; then
    echo "ERROR: something failed when running '$cmdline'"
    exit 1
fi

if ! diff -s narrowband1b_bins.txt narrowband1c_bins.txt; then
    echo "ERROR: narrowband1b_bins.txt and narrowband1c_bins.txt should be equal"
    exit 1
fi

## check narrowband1d/ SFT names (reduced range)
for freq in 10 18 26 34 42 50 58 66 74 82 90; do
    if [ $freq = 90 ]; then
        binwidth="0005Hz1"
    else
        binwidth="0008Hz0"
    fi
    sft="narrowband1d/H-3_H1_${Tsft}SFT_NBF00${freq}Hz0W${binwidth}_mfdv5-${startTime2}-${span2}.sft"
    if ! test -f $sft; then
        echo "ERROR: could not find file '$sft'"
        exit 1
    fi
done
