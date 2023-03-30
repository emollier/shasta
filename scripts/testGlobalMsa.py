#!/usr/bin/python3

import shasta

sequences = [
    ("AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ("AGGTCCGACAGCGCGCCCATACTCCAGCCACGCCACCGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ("AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACCGGTCCGCCTCTCCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ("AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACCGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 5),
    ("AGGTCCGACAGCGCCCAGATCCAGCCACGCCACCGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ("AGGTCCGACAGCGCGCCAGATCCAGCCACGCCACCGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ("AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACCGGTCCGCCTCTTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 2),
    ("AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACCGGTCCGCCGCTCGCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC", 1),
    ]
    
expectedConsensus = "AGGTCCGACAGCGCGCCCAGATCCAGCCACGCCACCGGTCCGCCTCTCCCGCCGCCCTGGCCTGTCCTTAGCCCCAGGC"


consensus = shasta.globalMsaPython(sequences, 30, 14)
print(consensus)

pureSpoaConsensus = shasta.globalMsaPython(sequences, 1000000000, 14)

if consensus == expectedConsensus:
    print("Consensus agrees with expected consensus.")
else:
    print("Consensus DOES NOT AGREE with expected consensus.")

if consensus == pureSpoaConsensus:
    print("Consensus agrees with pure spoa consensus.")
else:
    print("Consensus DOES NOT AGREE with pure spoa consensus.")

