// Unit tests for Poseidon/Foundation/Types/Pointers.hpp
// Testing smart pointers and reference counting

#define _CRT_SECURE_NO_WARNINGS
#include <catch2/catch_test_macros.hpp>
#include <Poseidon/Foundation/Framework/DebugLog.hpp>
#include <Poseidon/Foundation/Types/Pointers.hpp>
#include <thread>
#include <utility>
#include <vector>

// Test Helper Classes

// Simple class with reference counting
class TestRefCount : public RefCount
{
  public:
    int value;
    static int constructCount;
    static int destructCount;

    TestRefCount() : value(0) { constructCount++; }
    explicit TestRefCount(int v) : value(v) { constructCount++; }
    ~TestRefCount() override { destructCount++; }

    static void ResetCounters()
    {
        constructCount = 0;
        destructCount = 0;
    }
};

int TestRefCount::constructCount = 0;
int TestRefCount::destructCount = 0;

class RefOperationProbe
{
  public:
    int referenceCount = 0;
    int addCount = 0;
    int releaseCount = 0;

    int AddRef()
    {
        addCount++;
        return ++referenceCount;
    }

    int Release()
    {
        releaseCount++;
        return --referenceCount;
    }

    void ResetOperations()
    {
        addCount = 0;
        releaseCount = 0;
    }
};

// Simple class for SRef testing
class TestObject
{
  public:
    int value;
    static int constructCount;
    static int destructCount;

    TestObject() : value(0) { constructCount++; }
    explicit TestObject(int v) : value(v) { constructCount++; }
    ~TestObject() { destructCount++; }

    static void ResetCounters()
    {
        constructCount = 0;
        destructCount = 0;
    }
};

int TestObject::constructCount = 0;
int TestObject::destructCount = 0;

// Batch 1: RefCount - Basic Reference Counting

TEST_CASE("RefCount - Basic operations", "[types][pointers]")
{
    TestRefCount::ResetCounters();

    SECTION("Construction and destruction")
    {
        {
            TestRefCount* obj = new TestRefCount(42);
            REQUIRE(TestRefCount::constructCount == 1);
            REQUIRE(obj->value == 42);
            REQUIRE(obj->RefCounter() == 0); // No references yet
            delete obj;
        }
        REQUIRE(TestRefCount::destructCount == 1);
    }

    SECTION("AddRef increments counter")
    {
        TestRefCount* obj = new TestRefCount(10);

        REQUIRE(obj->RefCounter() == 0);
        obj->AddRef();
        REQUIRE(obj->RefCounter() == 1);
        obj->AddRef();
        REQUIRE(obj->RefCounter() == 2);

        // Clean up manually
        obj->Release();
        obj->Release();
    }

    SECTION("Release decrements counter")
    {
        TestRefCount* obj = new TestRefCount(20);
        obj->AddRef();
        obj->AddRef();

        REQUIRE(obj->RefCounter() == 2);
        int count = obj->Release();
        REQUIRE(count == 1);
        REQUIRE(obj->RefCounter() == 1);

        obj->Release();
    }

    SECTION("Release with zero count deletes object")
    {
        TestRefCount::ResetCounters();
        TestRefCount* obj = new TestRefCount(30);
        obj->AddRef();

        REQUIRE(TestRefCount::destructCount == 0);
        obj->Release(); // Should delete
        REQUIRE(TestRefCount::destructCount == 1);
    }
}

// Batch 2: Ref<T> - Smart Pointer

TEST_CASE("Ref - Construction", "[types][pointers]")
{
    TestRefCount::ResetCounters();

    SECTION("Default construction")
    {
        Ref<TestRefCount> ptr;

        REQUIRE(ptr.IsNull());
        REQUIRE_FALSE(ptr.NotNull());
    }

    SECTION("Construction from raw pointer")
    {
        TestRefCount* obj = new TestRefCount(100);
        Ref<TestRefCount> ptr(obj);

        REQUIRE(ptr.NotNull());
        REQUIRE(ptr->value == 100);
        REQUIRE(obj->RefCounter() == 1);
    }

    SECTION("Copy construction")
    {
        TestRefCount* obj = new TestRefCount(200);
        Ref<TestRefCount> ptr1(obj);
        Ref<TestRefCount> ptr2(ptr1);

        REQUIRE(ptr1.NotNull());
        REQUIRE(ptr2.NotNull());
        REQUIRE(obj->RefCounter() == 2);
        REQUIRE(ptr1->value == 200);
        REQUIRE(ptr2->value == 200);
    }
}

TEST_CASE("Ref - Assignment", "[types][pointers]")
{
    TestRefCount::ResetCounters();

    SECTION("Assignment from raw pointer")
    {
        Ref<TestRefCount> ptr;
        TestRefCount* obj = new TestRefCount(300);

        ptr = obj;

        REQUIRE(ptr.NotNull());
        REQUIRE(ptr->value == 300);
        REQUIRE(obj->RefCounter() == 1);
    }

    SECTION("Assignment from another Ref")
    {
        TestRefCount* obj = new TestRefCount(400);
        Ref<TestRefCount> ptr1(obj);
        Ref<TestRefCount> ptr2;

        ptr2 = ptr1;

        REQUIRE(ptr2.NotNull());
        REQUIRE(obj->RefCounter() == 2);
    }

    SECTION("Self-assignment")
    {
        TestRefCount* obj = new TestRefCount(500);
        Ref<TestRefCount> ptr(obj);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
        ptr = ptr; // Intentionally testing self-assignment
#ifdef __clang__
#pragma clang diagnostic pop
#endif

        REQUIRE(obj->RefCounter() == 1);
        REQUIRE(ptr->value == 500);
    }
}

TEST_CASE("Ref - Move operations", "[types][pointers]")
{
    SECTION("Move construction transfers ownership")
    {
        RefOperationProbe object;
        Ref<RefOperationProbe> source(&object);
        object.ResetOperations();

        Ref<RefOperationProbe> destination(std::move(source));

        REQUIRE(source.IsNull());
        REQUIRE(destination.GetRef() == &object);
        REQUIRE(object.referenceCount == 1);
        REQUIRE(object.addCount == 0);
        REQUIRE(object.releaseCount == 0);
    }

    SECTION("Move assignment transfers ownership")
    {
        RefOperationProbe object;
        Ref<RefOperationProbe> source(&object);
        Ref<RefOperationProbe> destination;
        object.ResetOperations();

        destination = std::move(source);

        REQUIRE(source.IsNull());
        REQUIRE(destination.GetRef() == &object);
        REQUIRE(object.referenceCount == 1);
        REQUIRE(object.addCount == 0);
        REQUIRE(object.releaseCount == 0);
    }

    SECTION("Move assignment releases existing ownership")
    {
        RefOperationProbe sourceObject;
        RefOperationProbe destinationObject;
        Ref<RefOperationProbe> source(&sourceObject);
        Ref<RefOperationProbe> destination(&destinationObject);
        sourceObject.ResetOperations();
        destinationObject.ResetOperations();

        destination = std::move(source);

        REQUIRE(source.IsNull());
        REQUIRE(destination.GetRef() == &sourceObject);
        REQUIRE(sourceObject.referenceCount == 1);
        REQUIRE(sourceObject.addCount == 0);
        REQUIRE(sourceObject.releaseCount == 0);
        REQUIRE(destinationObject.referenceCount == 0);
        REQUIRE(destinationObject.releaseCount == 1);
    }

    SECTION("Self move retains ownership")
    {
        RefOperationProbe object;
        Ref<RefOperationProbe> pointer(&object);
        Ref<RefOperationProbe>& alias = pointer;
        object.ResetOperations();

        pointer = std::move(alias);

        REQUIRE(pointer.GetRef() == &object);
        REQUIRE(object.referenceCount == 1);
        REQUIRE(object.addCount == 0);
        REQUIRE(object.releaseCount == 0);
    }
}

TEST_CASE("Ref - Reference counting", "[types][pointers]")
{
    TestRefCount::ResetCounters();

    SECTION("Multiple references")
    {
        TestRefCount* obj = new TestRefCount(600);
        Ref<TestRefCount> ptr1(obj);
        Ref<TestRefCount> ptr2(ptr1);
        Ref<TestRefCount> ptr3(ptr2);

        REQUIRE(obj->RefCounter() == 3);
    }

    SECTION("References going out of scope")
    {
        TestRefCount* obj = new TestRefCount(700);
        {
            Ref<TestRefCount> ptr1(obj);
            REQUIRE(obj->RefCounter() == 1);
            {
                Ref<TestRefCount> ptr2(ptr1);
                REQUIRE(obj->RefCounter() == 2);
            }
            REQUIRE(obj->RefCounter() == 1);
        }
        REQUIRE(TestRefCount::destructCount == 1);
    }
}

TEST_CASE("Ref - Operations", "[types][pointers]")
{
    SECTION("Free releases reference")
    {
        TestRefCount* obj = new TestRefCount(800);
        Ref<TestRefCount> ptr(obj);

        REQUIRE(ptr.NotNull());
        ptr.Free();
        REQUIRE(ptr.IsNull());
    }

    SECTION("Arrow operator")
    {
        TestRefCount* obj = new TestRefCount(900);
        Ref<TestRefCount> ptr(obj);

        ptr->value = 999;
        REQUIRE(ptr->value == 999);
    }

    SECTION("Implicit conversion to pointer")
    {
        TestRefCount* obj = new TestRefCount(1000);
        Ref<TestRefCount> ptr(obj);

        TestRefCount* raw = ptr;
        REQUIRE(raw == obj);
        REQUIRE(raw->value == 1000);
    }
}

// Batch 3: SRef<T> - Single-Owner Smart Pointer

TEST_CASE("SRef - Construction", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Default construction")
    {
        SRef<TestObject> ptr;

        REQUIRE(ptr.IsNull());
        REQUIRE(!ptr.NotNull());
    }

    SECTION("Construction from raw pointer")
    {
        SRef<TestObject> ptr(new TestObject(100));

        REQUIRE(ptr.NotNull());
        REQUIRE(ptr->value == 100);
    }
}

TEST_CASE("SRef - Ownership transfer", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Copy construction transfers ownership")
    {
        SRef<TestObject> ptr1(new TestObject(200));
        SRef<TestObject> ptr2(ptr1);

        REQUIRE(ptr1.IsNull()); // Ownership transferred
        REQUIRE(ptr2.NotNull());
        REQUIRE(ptr2->value == 200);
    }

    SECTION("Assignment transfers ownership")
    {
        SRef<TestObject> ptr1(new TestObject(300));
        SRef<TestObject> ptr2;

        ptr2 = ptr1;

        REQUIRE(ptr1.IsNull());
        REQUIRE(ptr2.NotNull());
        REQUIRE(ptr2->value == 300);
    }
}

TEST_CASE("SRef - Automatic deletion", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Destructor deletes object")
    {
        {
            SRef<TestObject> ptr(new TestObject(400));
            REQUIRE(TestObject::constructCount == 1);
            REQUIRE(TestObject::destructCount == 0);
        }
        REQUIRE(TestObject::destructCount == 1);
    }

    SECTION("Free deletes object")
    {
        TestObject::ResetCounters();
        SRef<TestObject> ptr(new TestObject(500));

        REQUIRE(TestObject::destructCount == 0);
        ptr.Free();
        REQUIRE(TestObject::destructCount == 1);
        REQUIRE(ptr.IsNull());
    }
}

TEST_CASE("SRef - Operations", "[types][pointers]")
{
    SECTION("Arrow operator")
    {
        SRef<TestObject> ptr(new TestObject(600));

        ptr->value = 666;
        REQUIRE(ptr->value == 666);
    }

    SECTION("Implicit conversion")
    {
        SRef<TestObject> ptr(new TestObject(700));

        TestObject* raw = ptr;
        REQUIRE(raw->value == 700);
    }

    SECTION("Bool conversion")
    {
        SRef<TestObject> ptr1;
        SRef<TestObject> ptr2(new TestObject(800));

        REQUIRE(!ptr1.NotNull());
        REQUIRE(ptr2.NotNull());
    }
}

// Batch 4: Temp<T> - Temporary Array

TEST_CASE("Temp - Construction", "[types][pointers]")
{
    SECTION("Default construction")
    {
        Temp<int> arr;

        REQUIRE(arr.IsNull());
        REQUIRE(arr.Size() == 0);
    }

    SECTION("Construction with size")
    {
        Temp<int> arr(5);

        REQUIRE(arr.NotNull());
        REQUIRE(arr.Size() == 5);
    }

    SECTION("Construction from array")
    {
        int src[] = {1, 2, 3, 4, 5};
        Temp<int> arr(src, 5);

        REQUIRE(arr.Size() == 5);
        REQUIRE(arr[0] == 1);
        REQUIRE(arr[4] == 5);
    }
}

TEST_CASE("Temp - Array access", "[types][pointers]")
{
    SECTION("Element access")
    {
        Temp<int> arr(3);
        arr[0] = 10;
        arr[1] = 20;
        arr[2] = 30;

        REQUIRE(arr.Get(0) == 10);
        REQUIRE(arr.Get(1) == 20);
        REQUIRE(arr.Get(2) == 30);
    }

    SECTION("Data pointer")
    {
        Temp<int> arr(3);
        int* data = arr.Data();

        data[0] = 100;
        data[1] = 200;
        data[2] = 300;

        REQUIRE(arr[0] == 100);
        REQUIRE(arr[1] == 200);
        REQUIRE(arr[2] == 300);
    }
}

TEST_CASE("Temp - Operations", "[types][pointers]")
{
    SECTION("Realloc changes size")
    {
        Temp<int> arr(3);
        arr[0] = 1;
        arr[1] = 2;
        arr[2] = 3;

        arr.Realloc(5);

        REQUIRE(arr.Size() == 5);
        // Old data is lost after realloc
    }

    SECTION("Free releases memory")
    {
        Temp<int> arr(10);

        arr.Free();

        REQUIRE(arr.IsNull());
    }

    SECTION("Export transfers ownership")
    {
        Temp<int> arr(3);
        arr[0] = 100;

        int* raw = arr.Export();

        REQUIRE(arr.IsNull());
        REQUIRE(raw[0] == 100);

        delete[] raw; // Must delete manually
    }
}

TEST_CASE("Temp - Copy operations", "[types][pointers]")
{
    SECTION("Copy construction")
    {
        Temp<int> arr1(3);
        arr1[0] = 10;
        arr1[1] = 20;
        arr1[2] = 30;

        Temp<int> arr2(arr1);

        REQUIRE(arr2.Size() == 3);
        REQUIRE(arr2[0] == 10);
        REQUIRE(arr2[2] == 30);
    }

    SECTION("Assignment")
    {
        Temp<int> arr1(2);
        arr1[0] = 5;
        arr1[1] = 10;

        Temp<int> arr2;
        arr2 = arr1;

        REQUIRE(arr2.Size() == 2);
        REQUIRE(arr2[0] == 5);
        REQUIRE(arr2[1] == 10);
    }
}

// Batch 5: APtr<T> - Array Pointer

TEST_CASE("APtr - Construction", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Default construction")
    {
        APtr<TestObject> arr;

        REQUIRE(arr.IsNull());
    }

    SECTION("Construction with size")
    {
        APtr<TestObject> arr(3);

        REQUIRE(arr.NotNull());
        REQUIRE(TestObject::constructCount == 3);
    }
}

TEST_CASE("APtr - Ownership", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Copy transfers ownership")
    {
        APtr<TestObject> arr1(2);
        APtr<TestObject> arr2(arr1);

        REQUIRE(arr1.IsNull());
        REQUIRE(arr2.NotNull());
    }

    SECTION("Assignment transfers ownership")
    {
        APtr<TestObject> arr1(3);
        APtr<TestObject> arr2;

        arr2 = arr1;

        REQUIRE(arr1.IsNull());
        REQUIRE(arr2.NotNull());
    }
}

TEST_CASE("APtr - Operations", "[types][pointers]")
{
    TestObject::ResetCounters();

    SECTION("Realloc")
    {
        APtr<TestObject> arr(2);
        int count1 = TestObject::constructCount;

        arr.Realloc(4);

        REQUIRE(arr.NotNull());
        REQUIRE(TestObject::constructCount > count1);
    }

    SECTION("Free")
    {
        APtr<TestObject> arr(3);

        arr.Free();

        REQUIRE(arr.IsNull());
    }

    SECTION("Automatic deletion")
    {
        TestObject::ResetCounters();
        {
            APtr<TestObject> arr(2);
            REQUIRE(TestObject::destructCount == 0);
        }
        REQUIRE(TestObject::destructCount == 2);
    }
}

// Batch 6: InitPtr<T> - Initialization Pointer

TEST_CASE("InitPtr - Basic operations", "[types][pointers]")
{
    SECTION("Default construction")
    {
        InitPtr<TestObject> ptr;

        REQUIRE(ptr.IsNull());
        REQUIRE(!ptr);
    }

    SECTION("Construction from pointer")
    {
        TestObject* obj = new TestObject(100);
        InitPtr<TestObject> ptr(obj);

        REQUIRE(ptr.NotNull());
        REQUIRE(ptr->value == 100);

        delete obj; // Must delete manually - InitPtr doesn't own
    }

    SECTION("Assignment")
    {
        TestObject* obj = new TestObject(200);
        InitPtr<TestObject> ptr;

        ptr = obj;

        REQUIRE(ptr.NotNull());
        REQUIRE(ptr->value == 200);

        delete obj;
    }

    SECTION("Arrow operator")
    {
        TestObject* obj = new TestObject(300);
        InitPtr<TestObject> ptr(obj);

        ptr->value = 333;
        REQUIRE(obj->value == 333);

        delete obj;
    }
}

// Batch 7: Edge Cases and Mixed Scenarios

TEST_CASE("Smart pointers - Null handling", "[types][pointers]")
{
    SECTION("Ref with null")
    {
        Ref<TestRefCount> ptr;

        REQUIRE(ptr.IsNull());
        ptr.Free(); // Should be safe
        REQUIRE(ptr.IsNull());
    }

    SECTION("SRef with null")
    {
        SRef<TestObject> ptr;

        REQUIRE(ptr.IsNull());
        ptr.Free(); // Should be safe
        REQUIRE(ptr.IsNull());
    }

    SECTION("Temp with zero size")
    {
        Temp<int> arr(0);

        // May be null or valid depending on implementation
        REQUIRE(arr.Size() == 0);
    }
}

TEST_CASE("Smart pointers - Complex scenarios", "[types][pointers]")
{
    TestRefCount::ResetCounters();

    SECTION("Ref array with reference counting")
    {
        Temp<Ref<TestRefCount>> refArray(3);
        TestRefCount* obj = new TestRefCount(1000);

        refArray[0] = obj;
        refArray[1] = obj;
        refArray[2] = obj;

        REQUIRE(obj->RefCounter() == 3);

        refArray.Free();
        REQUIRE(TestRefCount::destructCount == 1);
    }

    SECTION("Reassignment releases old object")
    {
        TestRefCount::ResetCounters();
        TestRefCount* obj1 = new TestRefCount(100);
        TestRefCount* obj2 = new TestRefCount(200);

        Ref<TestRefCount> ptr(obj1);
        REQUIRE(TestRefCount::destructCount == 0);

        ptr = obj2; // Should release obj1
        REQUIRE(TestRefCount::destructCount == 1);
        REQUIRE(ptr->value == 200);
    }
}

// RefCount::_count is touched concurrently: landscape shapes/textures are AddRef'd / Release'd
// from TaskPool threads during parallel terrain generation. A non-atomic ++/-- loses updates
// under contention -> under-count -> premature delete / double-free (the re-mount crash class).
// This hammers AddRef/Release from many threads against a base reference that keeps the count
// Every thread does only AddRef (no Release), so a correct atomic counter ends at exactly
// base + total increments. Cleanup releases down to 0 from whatever the actual count is, so it
// stays safe whichever way the counter behaved.
//
// Teeth (TSan): revert RefCount::_count to a plain int and run under linux-x64-clang-tsan —
// ThreadSanitizer reports a data race on RefCount::AddRef (the re-mount terrain-gen path takes
// the engine count to 0 races; a plain int restores ~100). With std::atomic the access is clean.
// The exact-count REQUIRE is a sanity check: lost updates are probabilistic, so on a fast x86 a
// plain int may still land exact in a given run — TSan, not the count, is the reliable detector.
TEST_CASE("RefCount AddRef is atomic under concurrency", "[pointers][refcount][threads][tsan]")
{
    TestRefCount::ResetCounters();
    TestRefCount* obj = new TestRefCount();
    obj->AddRef(); // base reference: count == 1

    constexpr int kThreads = 8;
    constexpr int kIters = 200000;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        workers.emplace_back(
            [obj]
            {
                for (int i = 0; i < kIters; ++i)
                {
                    obj->AddRef();
                }
            });
    }
    for (auto& w : workers)
    {
        w.join();
    }

    REQUIRE(obj->RefCounter() == 1 + kThreads * kIters); // no increment may be lost

    // Drop every reference (base + all the AddRefs) so the object is deleted exactly once.
    // Snapshot the count first: the final Release() deletes the object, so re-reading
    // RefCounter() in the loop condition would be a use-after-free.
    for (int refs = obj->RefCounter(); refs > 0; --refs)
    {
        obj->Release();
    }
    REQUIRE(TestRefCount::destructCount == 1);
}
