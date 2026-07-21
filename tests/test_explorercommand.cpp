#include <QtTest>
#include <windows.h>
#include <shobjidl.h>

namespace {

constexpr CLSID eddyCommandClsid = {
    0x9d42e9a3, 0x9c3b, 0x4e14, {0xa3, 0xed, 0x92, 0xd8, 0x51, 0x67, 0xd2, 0x35}
};

using GetClassObject = HRESULT (STDAPICALLTYPE *)(REFCLSID, REFIID, void **);

}

class TestExplorerCommand : public QObject {
    Q_OBJECT
private slots:
    void exportsModernExplorerCommand() {
        HMODULE module = LoadLibraryW(L"eddy_explorer_command.dll");
        QVERIFY(module != nullptr);

        auto getClassObject = reinterpret_cast<GetClassObject>(
            GetProcAddress(module, "DllGetClassObject"));
        QVERIFY(getClassObject != nullptr);

        IClassFactory *factory = nullptr;
        QCOMPARE(getClassObject(eddyCommandClsid, IID_PPV_ARGS(&factory)), S_OK);
        QVERIFY(factory != nullptr);

        IExplorerCommand *command = nullptr;
        QCOMPARE(factory->CreateInstance(nullptr, IID_PPV_ARGS(&command)), S_OK);
        QVERIFY(command != nullptr);

        PWSTR title = nullptr;
        QCOMPARE(command->GetTitle(nullptr, &title), S_OK);
        QCOMPARE(QString::fromWCharArray(title), QStringLiteral("Open with Eddy"));
        CoTaskMemFree(title);

        EXPCMDSTATE state = ECS_ENABLED;
        QCOMPARE(command->GetState(nullptr, FALSE, &state), S_OK);
        QCOMPARE(state, ECS_HIDDEN);

        command->Release();
        factory->Release();
        QVERIFY(FreeLibrary(module));
    }
};

QTEST_MAIN(TestExplorerCommand)
#include "test_explorercommand.moc"
