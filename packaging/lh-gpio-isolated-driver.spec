Name:           lh-gpio-isolated-driver
Version:        0.1.0
Release:        1%{?dist}
Summary:        lh-gpio-iso - Kuhn Digital Integrator Filter & CTR Saturation Voltage
License:        Apache-2.0
URL:            https://github.com/Logihertz-Systems-OPC-Pvt-Ltd/lh-gpio-isolated-driver
Source0:        %{name}-%{version}-src.tar.gz
Packager:       Sameer Tawade <ops@logihertz.com>
BuildArch:      %{_arch}

%description
C11 optocoupled industrial GPIO driver and simulator featuring a Kuhn digital integrator contact-bounce filter, edge interrupt-storm lockout guard, optocoupler CTR aging saturation analyzer, and Tegra234 Device-Tree overlay.

%prep
%autosetup -n %{name}-0.1.0

%build
# Industrial clean-room build with zero external runtime dependencies

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}%{_bindir}
mkdir -p %{buildroot}%{_docdir}/%{name}
mkdir -p %{buildroot}%{_mandir}/man1

if [ -f bin/lh-gpio-iso ]; then
    install -m 0755 bin/lh-gpio-iso %{buildroot}%{_bindir}/lh-gpio-iso
elif [ -f lh-gpio-iso ]; then
    install -m 0755 lh-gpio-iso %{buildroot}%{_bindir}/lh-gpio-iso
fi

install -m 0644 README.md %{buildroot}%{_docdir}/%{name}/README.md
install -m 0644 SPEC.md %{buildroot}%{_docdir}/%{name}/SPEC.md
install -m 0644 LICENSE %{buildroot}%{_docdir}/%{name}/LICENSE
install -m 0644 NOTICE %{buildroot}%{_docdir}/%{name}/NOTICE

%files
%doc %{_docdir}/%{name}/README.md
%doc %{_docdir}/%{name}/SPEC.md
%doc %{_docdir}/%{name}/NOTICE
%license %{_docdir}/%{name}/LICENSE

%changelog
* Mon Nov 16 2026 Sameer Tawade <ops@logihertz.com> - 0.1.0-1
- Initial production release for Logihertz Systems Ecosystem and STRUCT OS.
