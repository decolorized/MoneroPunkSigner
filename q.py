import urllib.request, re
url = "https://raw.githubusercontent.com/qt/qtbase/6.7/src/widgets/dialogs/qwizard.cpp"
src = urllib.request.urlopen(url, timeout=60).read().decode("utf-8", "replace")
open(r"qwizard_qt6.cpp","w",encoding="utf-8").write(src)
print("len", len(src))
for fn in ["bool QWizardPrivate::ensureButton", "void QWizardPrivate::updateButtonTexts",
           "void QWizardPrivate::_q_updateButtonStates", "void QWizard::next",
           "void QWizard::accept", "bool QWizard::validateCurrentPage"]:
    i = src.find(fn)
    print(f"\n########## {fn} @ {i}")
    if i >= 0:
        print(src[i:i+1800])
