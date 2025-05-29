Problem:    
Rows:       40
Columns:    34
Non-zeros:  171
Status:     OPTIMAL
Objective:  obj = 0.8 (MAXimum)

   No.   Row name   St   Activity     Lower bound   Upper bound    Marginal
------ ------------ -- ------------- ------------- ------------- -------------
     1 ccapr1r2     B              2                           3 
     2 ccapr2r1     B            1.6                           3 
     3 ccapr1r3     B              6                          10 
     4 ccapr3r1     B              0                          10 
     5 ccapr2r3     B              0                           8 
     6 ccapr3r2     NU             8                           8         < eps
     7 ccapr2r4     NU            10                          10          0.04 
     8 ccapr4r2     B            1.6                          10 
     9 ccapr3r4     NU            10                          10          0.04 
    10 ccapr4r3     B              0                          10 
    11 crmax1       B              8                          10 
    12 crmax2       B            1.6                           2 
    13 crmax3       B             12                          15 
    14 cratio1      NL             0             0                       -0.04 
    15 cratio2      NL             0             0                       < eps
    16 cratio3      NL             0             0                       -0.04 
    17 cflow1r1ge   NL             0             0                       -0.04 
    18 cflow1r1le   B              0                           0 
    19 cflow1r2ge   NL             0             0                       -0.04 
    20 cflow1r2le   B              0                           0 
    21 cflow1r3ge   NL             0             0                       -0.04 
    22 cflow1r3le   B              0                           0 
    23 cflow1r4ge   B              0             0               
    24 cflow1r4le   B              0                           0 
    25 cflow2r1ge   B              0             0               
    26 cflow2r1le   NU             0                           0         < eps
    27 cflow2r2ge   B              0             0               
    28 cflow2r2le   B              0                           0 
    29 cflow2r3ge   B              0             0               
    30 cflow2r3le   B              0                           0 
    31 cflow2r4ge   NL             0             0                       < eps
    32 cflow2r4le   B              0                           0 
    33 cflow3r1ge   NL             0             0                       -0.04 
    34 cflow3r1le   B              0                           0 
    35 cflow3r2ge   NL             0             0                       -0.04 
    36 cflow3r2le   B              0                           0 
    37 cflow3r3ge   NL             0             0                       -0.04 
    38 cflow3r3le   B              0                           0 
    39 cflow3r4ge   B              0             0               
    40 cflow3r4le   B              0                           0 

   No. Column name  St   Activity     Lower bound   Upper bound    Marginal
------ ------------ -- ------------- ------------- ------------- -------------
     1 ratiomin     B            0.8             0               
     2 fr1r21       B              2             0               
     3 fr1r22       NL             0             0                       < eps
     4 fr1r23       NL             0             0                       < eps
     5 fr2r11       NL             0             0                       < eps
     6 fr2r12       B            1.6             0               
     7 fr2r13       NL             0             0                       < eps
     8 fr1r31       B              6             0               
     9 fr1r32       NL             0             0                       < eps
    10 fr1r33       NL             0             0                       < eps
    11 fr3r11       NL             0             0                       < eps
    12 fr3r12       NL             0             0                       < eps
    13 fr3r13       B              0             0               
    14 fr2r31       NL             0             0                       < eps
    15 fr2r32       NL             0             0                       < eps
    16 fr2r33       NL             0             0                       < eps
    17 fr3r21       NL             0             0                       < eps
    18 fr3r22       NL             0             0                       < eps
    19 fr3r23       B              8             0               
    20 fr2r41       B              2             0               
    21 fr2r42       NL             0             0                       -0.04 
    22 fr2r43       B              8             0               
    23 fr4r21       NL             0             0                       -0.04 
    24 fr4r22       B            1.6             0               
    25 fr4r23       NL             0             0                       -0.04 
    26 fr3r41       B              6             0               
    27 fr3r42       NL             0             0                       -0.04 
    28 fr3r43       B              4             0               
    29 fr4r31       NL             0             0                       -0.04 
    30 fr4r32       NL             0             0                       < eps
    31 fr4r33       NL             0             0                       -0.04 
    32 rstar1       B              8             0               
    33 rstar2       B            1.6             0               
    34 rstar3       B             12             0               

Karush-Kuhn-Tucker optimality conditions:

KKT.PE: max.abs.err = 3.55e-15 on row 14
        max.rel.err = 2.09e-16 on row 14
        High quality

KKT.PB: max.abs.err = 3.55e-15 on row 40
        max.rel.err = 3.55e-15 on row 40
        High quality

KKT.DE: max.abs.err = 1.11e-16 on column 1
        max.rel.err = 3.70e-17 on column 1
        High quality

KKT.DB: max.abs.err = 3.38e-17 on column 30
        max.rel.err = 3.38e-17 on column 30
        High quality

End of output
