#include "test.h"
#include "sequence.h"

#include "alloc.h"

static Sequence *SequenceCreateRange(size_t initialCapacity, size_t start, size_t end)
{
    Sequence *seq = SequenceCreate(initialCapacity, free);

    for (size_t i = start; i <= end; i++)
    {
        size_t *item = xmalloc(sizeof(size_t));

        *item = i;
        SequenceAppend(seq, item);
    }

    return seq;
}

static void test_create_destroy(void **state)
{
    Sequence *seq = SequenceCreate(5, NULL);

    SequenceDestroy(seq);
}

static void test_append(void **state)
{
    Sequence *seq = SequenceCreate(2, free);

    for (size_t i = 0; i < 1000; i++)
    {
        SequenceAppend(seq, xstrdup("snookie"));
    }

    assert_int_equal(seq->length, 1000);

    for (size_t i = 0; i < 1000; i++)
    {
        assert_string_equal(seq->data[i], "snookie");
    }

    SequenceDestroy(seq);
}

static int CompareNumbers(const void *a, const void *b)
{
    return *(size_t *) a - *(size_t *) b;
}

static void test_lookup(void **state)
{
    Sequence *seq = SequenceCreateRange(10, 0, 9);

    size_t *key = xmalloc(sizeof(size_t));

    *key = 5;

    size_t *result = SequenceLookup(seq, key, CompareNumbers);

    assert_int_equal(*result, *key);

    *key = 17;
    result = SequenceLookup(seq, key, CompareNumbers);
    assert_int_equal(result, NULL);

    SequenceDestroy(seq);
    free(key);
}

static void test_sort(void **state)
{
    Sequence *seq = SequenceCreate(5, NULL);

    size_t one = 1;
    size_t two = 2;
    size_t three = 3;
    size_t four = 4;
    size_t five = 5;

    SequenceAppend(seq, &three);
    SequenceAppend(seq, &two);
    SequenceAppend(seq, &five);
    SequenceAppend(seq, &one);
    SequenceAppend(seq, &four);

    SequenceSort(seq, CompareNumbers);

    assert_int_equal(seq->data[0], &one);
    assert_int_equal(seq->data[1], &two);
    assert_int_equal(seq->data[2], &three);
    assert_int_equal(seq->data[3], &four);
    assert_int_equal(seq->data[4], &five);

    SequenceDestroy(seq);
}

static void test_remove_range(void **state)
{

    Sequence *seq = SequenceCreateRange(10, 0, 9);

    SequenceRemoveRange(seq, 3, 9);
    assert_int_equal(seq->length, 3);
    assert_int_equal(*(size_t *) seq->data[0], 0);
    assert_int_equal(*(size_t *) seq->data[1], 1);
    assert_int_equal(*(size_t *) seq->data[2], 2);

    SequenceDestroy(seq);
    seq = SequenceCreateRange(10, 0, 9);

    SequenceRemoveRange(seq, 0, 2);
    assert_int_equal(seq->length, 7);
    assert_int_equal(*(size_t *) seq->data[0], 3);

    SequenceDestroy(seq);

    seq = SequenceCreateRange(10, 0, 9);

    SequenceRemoveRange(seq, 5, 5);
    assert_int_equal(seq->length, 9);
    assert_int_equal(*(size_t *) seq->data[5], 6);

    SequenceDestroy(seq);
}

int main()
{
    const UnitTest tests[] =
{
        unit_test(test_create_destroy),
        unit_test(test_append),
        unit_test(test_lookup),
        unit_test(test_sort),
        unit_test(test_remove_range)
    };

    return run_tests(tests);
}
