# Postmortem

Short engineering notes from a discontinued research project. This is not a plan to resume it.

## Boundaries were useful

Foundation code stayed free of GTA types. The adapter owned detection, fingerprints, and research reads. Tests could run without a game process. That split made it possible to say which claims were offline and which required a live process.

## Unsupported has to stay unsupported

The studied executable was detected and hashed. The supported-build list stayed empty and readiness stayed `NOT_IMPLEMENTED`. Filling either field without a qualified player path would have turned a fingerprint into a false compatibility claim.

## Static signatures were not enough

Generic RIP-relative patterns and string hits (`GWorld`, `PersistentLevel`, `OwningGameInstance`) produced candidates and, in the structural study, tens of thousands of references. Convergence and one weak identity label still did not identify a world pointer. A stale legacy address did not even fall inside the image.

## Synthetic tests are not live integration

Harnesses, fixtures, and foundation tests check the project's own code. They do not spawn remote players or synchronize a session inside GTA. A passing suite is evidence about the suite.

## Negative results are part of the record

ASCII scans, ambiguous patterns, a legacy slot that went null in gameplay, and a metadata study that did not justify an experiment all ruled out specific methods on a specific build. They did not prove that no method exists. Keeping the negative results visible prevents the next reader from repeating a closed path as if it were new.

## Research infrastructure grew past the result

Loaders, oracle watchers, phase protocols, and scan instrumentation became larger than the unanswered question: is there a stable local-player transform on this build? The machinery could be tested thoroughly while the live question stayed open.

## Small live proofs first

If someone continues this kind of work, the useful order is a small, explicit live proof before more framework. This snapshot does not start that work.
