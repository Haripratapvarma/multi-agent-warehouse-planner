// The SafeHandle tests count live native objects, a number shared by the
// whole process. Running test classes one after another keeps that count
// meaningful.
[assembly: CollectionBehavior(DisableTestParallelization = true)]
