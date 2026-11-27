# TestFlight and App Store checklist

Facts below were checked against Apple's developer pages on 10 Oct 2026; re-check before submitting as rules change.

## Account
- [ ] Apple Account with two-factor authentication.
- [ ] Enrol in the Apple Developer Program ($99/yr) - as an individual (your legal name shows as seller) or an organisation (needs D-U-N-S number). Approval time is not guaranteed by Apple; enrol early.
- [ ] Accept agreements in App Store Connect. If you will charge money: Paid Apps agreement, banking, tax forms.
- [ ] EU: declare trader status (Digital Services Act) if distributing in the EU.

## Identifiers (decide BEFORE first upload)
- [ ] Final app name (30 chars, must be unique on the store), bundle id, AUv3 extension bundle id (prefix of the app's).
- [ ] AU component codes: type `aumu`, subtype and manufacturer (4 chars each; manufacturer must have at least one uppercase letter). Changing them later breaks saved sessions in DAWs.

## Build
- [ ] Built with Xcode 26+ / iOS 26 SDK (mandatory for uploads since 28 Apr 2026).
- [ ] Deployment target >= iOS 13 (currently 16.0).
- [ ] Privacy manifest (`PrivacyInfo.xcprivacy`) in app AND extension; declare every required-reason API actually used (e.g. UserDefaults, file timestamps). Review after each new feature.
- [ ] `ITSAppUsesNonExemptEncryption` = false (set) unless you add custom crypto.
- [ ] Microphone usage string present (set) - needed for the sampler/recorder.
- [ ] Real 1024x1024 app icon (current one is a placeholder), no transparency.
- [ ] Archive (Product > Archive) > Distribute > App Store Connect > Upload.

## App Store Connect record
- [ ] New app: name, primary language, bundle id, SKU.
- [ ] Privacy policy URL (required). If you collect nothing, say so; GitHub Pages can host it.
- [ ] App Privacy "nutrition label" answers (likely "Data Not Collected" if there is no analytics/accounts).
- [ ] Age rating questionnaire (updated questions since Jan 2026).
- [ ] Category (Music), keywords, description, support URL, copyright.
- [ ] Screenshots for required iPhone and iPad sizes (App Store Connect lists current sizes); optional app preview video.
- [ ] Pricing and availability.
- [ ] Review notes: explain it is a synthesiser and an AUv3 plug-in, how to test it (include a short video of it in a host), no login needed.
- [ ] Export compliance / content rights answers.

## TestFlight
- [ ] Upload build; wait for processing; answer encryption question.
- [ ] Internal testing (up to 100 App Store Connect users) - no review, available quickly.
- [ ] External testing (public link/groups) - first build needs Beta App Review; fill "What to test" and contact info.
- [ ] Builds expire after 90 days.

## Submission
- [ ] Select build, complete all metadata, "Submit for Review". Typical review is a day or two; fix and resubmit on rejection.
- [ ] Common synth/AUv3 rejection causes: crashes on launch, missing privacy policy, placeholder content, extension that does not load, unexplained mic permission.
