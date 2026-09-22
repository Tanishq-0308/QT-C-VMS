// tst_comptr: reference-count correctness of decklink/com_ptr.h and of the
// DeckLinkInputDevice constructor/destructor (decklink/DeckLinkInputDevice.cpp).
//
// Uses fake IUnknown-derived objects that count AddRef/Release but never
// delete themselves, so every imbalance is observable as a number.

#include <QtTest>
#include <QCoreApplication>
#include <atomic>
#include <cstring>
#include <new>
#include <type_traits>

#include "DeckLinkAPI.h"
#include "com_ptr.h"
#include "DeckLinkInputDevice.h"

namespace {

// A private IID for the fake "second interface".
static const REFIID IID_FakeOther = { 0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                                      0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00 };

inline bool sameIID(const REFIID& a, const REFIID& b) { return std::memcmp(&a, &b, sizeof(REFIID)) == 0; }

class FakeUnknown : public IUnknown
{
public:
    std::atomic<long> refs{1};      // the test itself owns the initial ref
    std::atomic<long> minRefsSeen{1};
    long queries = 0;

    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override
    {
        ++queries;
        if (!ppv) return E_INVALIDARG;
        if (sameIID(iid, IID_IUnknown) || sameIID(iid, IID_FakeOther)) {
            *ppv = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return (ULONG)++refs; }
    ULONG Release() override
    {
        long v = --refs;
        if (v < minRefsSeen) minRefsSeen = v;
        return (ULONG)v; // never deletes: lets the test observe over-release
    }
    virtual ~FakeUnknown() = default;
};

// Mock IDeckLink: implements every pure virtual; every sub-interface query fails
// (like a device without input), which is the path HomePage::addDevice takes
// when Init() returns false.
class MockDeckLink : public IDeckLink
{
public:
    std::atomic<long> refs{1};
    QStringList queried;

    HRESULT QueryInterface(REFIID iid, LPVOID* ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        if (sameIID(iid, IID_IDeckLinkInput)) queried << "IDeckLinkInput";
        else if (sameIID(iid, IID_IDeckLinkConfiguration)) queried << "IDeckLinkConfiguration";
        else if (sameIID(iid, IID_IDeckLinkHDMIInputEDID)) queried << "IDeckLinkHDMIInputEDID";
        else if (sameIID(iid, IID_IDeckLinkProfileAttributes)) queried << "IDeckLinkProfileAttributes";
        else queried << "other";
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return (ULONG)++refs; }
    ULONG Release() override { return (ULONG)--refs; }
    HRESULT GetModelName(const char** n) override { if (n) *n = nullptr; return E_NOTIMPL; }
    HRESULT GetDisplayName(const char** n) override { if (n) *n = nullptr; return E_NOTIMPL; }
    ~MockDeckLink() override = default;
};

} // namespace

class TstComPtr : public QObject
{
    Q_OBJECT
private slots:
    // ---- com_ptr basics ---------------------------------------------------
    void layoutIsSinglePointer();
    void rawCtorBalances();
    void copyCtorBalances();
    void moveCtorBalances();
    void copyAssignBalances();
    void copyAssignSelfBalances();
    void moveAssignBalances();
    void selfMoveAssign();
    void assignNullptrBalances();
    void makeComPtrBalances();
    void iidCtorFromValidQueriesAndBalances();
    void iidCtorFromNullLeavesNull();
    void releaseAndGetAddressOf_nullsPointer();
    void releaseAndGetAddressOf_calleeWritesNothing_noOverRelease();
    void queryInterfaceIntoFreshComPtr();
    void queryInterfaceIntoNonEmptyComPtr();
    // ---- DeckLinkInputDevice ---------------------------------------------
    void deckLinkInputDevice_refcountRestoredAfterDestruction();
};

void TstComPtr::layoutIsSinglePointer()
{
    // The "QueryInterface(iid, (void**)&com_ptr)" pattern (HomePage.cpp addDevice /
    // reconfigureVideoInput) is only valid if com_ptr<T> is layout-identical to T*.
    QCOMPARE(sizeof(com_ptr<IUnknown>), sizeof(IUnknown*));
    QVERIFY(std::is_standard_layout<com_ptr<IUnknown>>::value);
}

void TstComPtr::rawCtorBalances()
{
    FakeUnknown o;
    {
        com_ptr<IUnknown> p(&o);
        QCOMPARE(o.refs.load(), 2L);
    }
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::copyCtorBalances()
{
    FakeUnknown o;
    {
        com_ptr<IUnknown> a(&o);
        {
            com_ptr<IUnknown> b(a);
            QCOMPARE(o.refs.load(), 3L);
            QCOMPARE(b.get(), a.get());
        }
        QCOMPARE(o.refs.load(), 2L);
    }
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::moveCtorBalances()
{
    FakeUnknown o;
    {
        com_ptr<IUnknown> a(&o);
        com_ptr<IUnknown> b(std::move(a));
        QCOMPARE(o.refs.load(), 2L);
        QVERIFY(!a);
        QCOMPARE(b.get(), static_cast<IUnknown*>(&o));
    }
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::copyAssignBalances()
{
    FakeUnknown o1, o2;
    {
        com_ptr<IUnknown> a(&o1);
        com_ptr<IUnknown> b(&o2);
        b = a;
        QCOMPARE(o1.refs.load(), 3L);
        QCOMPARE(o2.refs.load(), 1L);
    }
    QCOMPARE(o1.refs.load(), 1L);
    QCOMPARE(o2.refs.load(), 1L);
}

void TstComPtr::copyAssignSelfBalances()
{
    FakeUnknown o;
    {
        com_ptr<IUnknown> a(&o);
        com_ptr<IUnknown>& alias = a;
        a = alias;
        QCOMPARE(o.refs.load(), 2L);
        QCOMPARE(a.get(), static_cast<IUnknown*>(&o));
    }
    QCOMPARE(o.refs.load(), 1L);
    QCOMPARE(o.minRefsSeen.load(), 1L);
}

void TstComPtr::moveAssignBalances()
{
    FakeUnknown o1, o2;
    {
        com_ptr<IUnknown> a(&o1);
        com_ptr<IUnknown> b(&o2);
        b = std::move(a);
        QVERIFY(!a);
        QCOMPARE(o1.refs.load(), 2L);
        QCOMPARE(o2.refs.load(), 1L);
    }
    QCOMPARE(o1.refs.load(), 1L);
    QCOMPARE(o2.refs.load(), 1L);
}

void TstComPtr::selfMoveAssign()
{
    FakeUnknown o;
    long heldAfter = -1;
    bool nonNullAfter = false;
    {
        com_ptr<IUnknown> a(&o);
        com_ptr<IUnknown>& alias = a;
        a = std::move(alias);   // operator=(com_ptr&&) with &other == this
        nonNullAfter = static_cast<bool>(a);
        heldAfter = o.refs.load();
        qInfo("self-move-assign: pointer %s, refs=%ld",
              nonNullAfter ? "retained" : "became NULL (object dropped)", heldAfter);
        // Invariant: refcount must equal baseline(1) + number of com_ptr holders.
        QCOMPARE(heldAfter, 1L + (nonNullAfter ? 1L : 0L));
    }
    QCOMPARE(o.refs.load(), 1L);
    QCOMPARE(o.minRefsSeen.load(), 1L); // never over-released
}

void TstComPtr::assignNullptrBalances()
{
    FakeUnknown o;
    com_ptr<IUnknown> a(&o);
    a = nullptr;
    QVERIFY(!a);
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::makeComPtrBalances()
{
    // make_com_ptr adopts the creation reference.  Use a heap FakeUnknown whose
    // initial ref is the "creation" ref.
    auto* raw = new FakeUnknown;
    {
        com_ptr<FakeUnknown> p(raw);
        raw->Release(); // mirror make_com_ptr's adoption
        QCOMPARE(raw->refs.load(), 1L);
    }
    QCOMPARE(raw->refs.load(), 0L);
    delete raw;
}

void TstComPtr::iidCtorFromValidQueriesAndBalances()
{
    FakeUnknown o;
    {
        com_ptr<IUnknown> src(&o);
        {
            com_ptr<IUnknown> q(IID_FakeOther, src);
            QVERIFY(q);
            QCOMPARE(o.refs.load(), 3L);
        }
        {
            com_ptr<IUnknown> q(IID_IDeckLinkInput, src); // unsupported
            QVERIFY(!q);
        }
        QCOMPARE(o.refs.load(), 2L);
    }
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::iidCtorFromNullLeavesNull()
{
    // com_ptr(REFIID, com_ptr<U> other): when other is NULL the body is skipped
    // and m_ptr is never initialised.  Construct into storage pre-filled with a
    // sentinel so an uninitialised member is detected deterministically.
    alignas(com_ptr<IUnknown>) unsigned char storage[sizeof(com_ptr<IUnknown>)];
    std::memset(storage, 0xAB, sizeof(storage));
    com_ptr<IUnknown> empty;
    auto* p = new (storage) com_ptr<IUnknown>(IID_FakeOther, empty);
    IUnknown* value = p->get();
    const bool isNull = (value == nullptr);
    if (isNull) p->~com_ptr<IUnknown>(); // only safe to destroy if NULL
    QVERIFY2(isNull, qPrintable(QString("com_ptr(REFIID, <null com_ptr>) left m_ptr uninitialised = %1 "
                                        "(destructor would call Release() on garbage)")
                                .arg(quintptr(value), 0, 16)));
}

void TstComPtr::releaseAndGetAddressOf_nullsPointer()
{
    FakeUnknown o;
    com_ptr<IUnknown> p(&o);
    QCOMPARE(o.refs.load(), 2L);
    IUnknown** slot = p.releaseAndGetAddressOf();
    QCOMPARE(o.refs.load(), 1L);   // it did release
    const bool nulled = (*slot == nullptr);
    if (!nulled) *slot = nullptr;  // prevent the destructor from over-releasing in THIS test
    QVERIFY2(nulled, "releaseAndGetAddressOf() released the object but left m_ptr pointing at it "
                     "(dangling pointer; destructor/next call would Release() again)");
}

void TstComPtr::releaseAndGetAddressOf_calleeWritesNothing_noOverRelease()
{
    // Pattern from DeckLinkInputDevice::queryDisplayModes():
    //   if (m_deckLinkInput->GetDisplayModeIterator(it.releaseAndGetAddressOf()) != S_OK) return;
    // If the callee fails without writing the out-param, the com_ptr still holds
    // the stale pointer and releases it again in its destructor.
    FakeUnknown o;
    o.AddRef(); // pretend a previous out-call gave the com_ptr one owned reference
    QCOMPARE(o.refs.load(), 2L);
    {
        com_ptr<IUnknown> p;
        *p.releaseAndGetAddressOf() = &o;   // adopt: refs stays 2 (p owns 1)
        (void)p.releaseAndGetAddressOf();   // failing callee: writes nothing
    }                                       // ~com_ptr
    QVERIFY2(o.refs.load() == 1L,
             qPrintable(QString("expected refs=1 after scope, got %1: stale pointer released twice")
                        .arg(o.refs.load())));
    QCOMPARE(o.minRefsSeen.load(), 1L);
}

void TstComPtr::queryInterfaceIntoFreshComPtr()
{
    // HomePage.cpp addDevice()/reconfigureVideoInput():
    //   com_ptr<IDeckLinkConfiguration> config;
    //   deckLink->QueryInterface(IID_..., reinterpret_cast<void**>(&config));
    FakeUnknown o;
    {
        com_ptr<IUnknown> q;
        QCOMPARE(o.QueryInterface(IID_FakeOther, reinterpret_cast<void**>(&q)), S_OK);
        QVERIFY(q);
        QCOMPARE(o.refs.load(), 2L);
    }
    QCOMPARE(o.refs.load(), 1L);
}

void TstComPtr::queryInterfaceIntoNonEmptyComPtr()
{
    // QueryInterface into a com_ptr that already holds an object. Writing through
    // &q would overwrite the raw slot and never release the old object, so
    // production code uses releaseAndGetAddressOf(), which releases it first.
    FakeUnknown oldObj, newObj;
    {
        com_ptr<IUnknown> q(&oldObj);
        QCOMPARE(newObj.QueryInterface(IID_FakeOther, reinterpret_cast<void**>(q.releaseAndGetAddressOf())), S_OK);
    }
    QCOMPARE(newObj.refs.load(), 1L);
    QVERIFY2(oldObj.refs.load() == 1L,
             qPrintable(QString("QueryInterface into non-empty com_ptr leaked previous object: refs=%1 (expected 1)")
                        .arg(oldObj.refs.load())));
}

void TstComPtr::deckLinkInputDevice_refcountRestoredAfterDestruction()
{
    MockDeckLink mock;               // refs = 1 (baseline owned by the test)
    const long baseline = mock.refs.load();
    {
        com_ptr<IDeckLink> deckLink(&mock);   // as delivered by DeckLinkDeviceDiscovery
        const long withCaller = mock.refs.load();
        QCOMPARE(withCaller, baseline + 1);
        {
            // Exactly as HomePage::addDevice(): make_com_ptr<DeckLinkInputDevice>(this, deckLink)
            auto dev = make_com_ptr<DeckLinkInputDevice>(nullptr, deckLink);
            const long whileAlive = mock.refs.load();
            qInfo("IDeckLink refs: caller-held=%ld, while DeckLinkInputDevice alive=%ld "
                  "(expected %ld: one ref for m_deckLink)", withCaller, whileAlive, withCaller + 1);
            QVERIFY(!dev->Init()); // mock has no IDeckLinkInput -> Init() fails early, as HomePage handles
            QVERIFY(mock.queried.contains("IDeckLinkInput"));
            Q_UNUSED(whileAlive);
        }   // last com_ptr dropped -> DeckLinkInputDevice::Release() -> delete this
        const long afterDestroy = mock.refs.load();
        QVERIFY2(afterDestroy == withCaller,
                 qPrintable(QString("IDeckLink refcount after DeckLinkInputDevice destruction = %1, expected %2 "
                                    "(leaked %3 reference(s))")
                            .arg(afterDestroy).arg(withCaller).arg(afterDestroy - withCaller)));
    }
    QCOMPARE(mock.refs.load(), baseline);
}

QTEST_GUILESS_MAIN(TstComPtr)
#include "tst_comptr.moc"
