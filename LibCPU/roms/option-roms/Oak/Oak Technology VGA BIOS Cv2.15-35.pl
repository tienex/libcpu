#!/usr/bin/perl -w

# HARDCODED filenames
open(my $FILE, "OAK.bin") or die $!;
binmode($FILE);
open(my $OUT, '>:raw', 'OAK1.bin') or die $!;

$max=32768;

# read file to $ibuf
for ($i=0;$i<$max;$i++) {
   read($FILE, my $byte, 1);
   $ibuf[$i]=ord( $byte);
};


for ($i=0;$i<$max;$i++) {

# translate data byte
# D0 -> D7
# D1 -> D5
# D2 -> D3
# D3 -> D2
# D4 -> D1
# D5 -> D0
# D6 -> D6
# D7 -> D4

   $data=0;
   if ( $ibuf[$i] & 1     ) {$data=$data | (1<<7)};
   if ( $ibuf[$i] & (1<<1)) {$data=$data | (1<<5)};
   if ( $ibuf[$i] & (1<<2)) {$data=$data | (1<<3)};
   if ( $ibuf[$i] & (1<<3)) {$data=$data | (1<<2)};
   if ( $ibuf[$i] & (1<<4)) {$data=$data | (1<<1)};
   if ( $ibuf[$i] & (1<<5)) {$data=$data | (1)};
   if ( $ibuf[$i] & (1<<6)) {$data=$data | (1<<6)};
   if ( $ibuf[$i] & (1<<7)) {$data=$data | (1<<4)};

# translate address
# A00 -> A07
# A01 -> A06
# A02 -> A05
# A03 -> A04
# A04 -> A03
# A05 -> A02
# A06 -> A01
# A07 -> A00
# A08 -> A11
# A09 -> A14
# A10 -> A13
# A11 -> A10
# A12 -> A09
# A13 -> A12 
# A14 -> A08 

   $a=0;
   if ( $i & 1      ) {$a = $a | (1<<7 )};
   if ( $i & (1<<1 )) {$a = $a | (1<<6 )};
   if ( $i & (1<<2 )) {$a = $a | (1<<5 )};
   if ( $i & (1<<3 )) {$a = $a | (1<<4 )};
   if ( $i & (1<<4 )) {$a = $a | (1<<3 )};
   if ( $i & (1<<5 )) {$a = $a | (1<<2 )};
   if ( $i & (1<<6 )) {$a = $a | (1<<1 )};
   if ( $i & (1<<7 )) {$a = $a | (1)    };
   if ( $i & (1<<8 )) {$a = $a | (1<<11)};
   if ( $i & (1<<9 )) {$a = $a | (1<<14)};
   if ( $i & (1<<10)) {$a = $a | (1<<13)};
   if ( $i & (1<<11)) {$a = $a | (1<<10)};
   if ( $i & (1<<12)) {$a = $a | (1<<9 )};
   if ( $i & (1<<13)) {$a = $a | (1<<12)};
   if ( $i & (1<<14)) {$a = $a | (1<<8 )};

# debug
   printf ("(%04X) %02X -> (%04X) %02X\n", $i,$ibuf[$i],$a,$data);

   $obuf[$a]=$data;
}


# write buffer
for ($i=0;$i<$max;$i++) {

   print $OUT pack('C', $obuf[$i] ) ;

}

close $FILE;
close $OUT;

