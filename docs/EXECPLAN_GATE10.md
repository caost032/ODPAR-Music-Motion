# Gate 10 execution plan / closure criteria

Gate 10 closes locally only when all of the following hold on one frozen source identity:

1. signed `.odparms` still passes the full Gate 5 verifier before Studio admission;
2. signer-independent package-content SHA is independently reconstructed from the signed manifest and decoded ZIP entries;
3. identical content re-signed with another valid key retains content identity while exact package SHA changes;
4. changed signed DATA/MEDIA changes content identity;
5. 576-byte revision records revalidate ODMC payload SHA and independently recomputed `revision_id`;
6. genesis project ID is independently recomputed and child revisions preserve it;
7. child revision links exact parent, rejects semantic no-op and rejects silent signer substitution;
8. revision-bound Preview validates Render IR + `analysis.bin` and selects canonical Music Map tick internally;
9. 320-byte Preview approval independently recomputes `approval_id` and binds revision/IR/canonical frame/preview frame identities;
10. Studio master plan matches Gate 9 package/Score/capability/Map/policy/IR/time/seed identities;
11. a stale/wrong package is rejected before master sink `begin`;
12. Music Spine registers Revision, Approval and Workflow with real acyclic dependencies and executable invariants;
13. architectural guardians include Studio in no-wallclock/no-partial-master scope;
14. independent Studio oracle passes under GCC and Clang;
15. strict GCC and Clang matrices pass without warning relaxation;
16. ASan+UBSan GCC, ASan+UBSan Clang and TSan pass;
17. GCC `-fanalyzer` passes all production translation units;
18. three clean-root builds are byte-identical;
19. all final lane receipts bind the same `source_id`;
20. a physical reproducible Gate 10 ZIP and `gate10_evidence.json` are created before Gate 11 begins.

Production Studio UI/runtime and real cloud deployment remain NOT_RUN unless exercised in their real environments.
