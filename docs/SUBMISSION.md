# GitHub Submission Steps

```bash
cd sensorhub-cuse
git init && git branch -M main
git add .
git commit -m "Capstone: SensorHub - C++ CUSE character device driver"
# create an EMPTY public repo on github.com first, then:
git remote add origin https://github.com/<your-username>/sensorhub-cuse.git
git push -u origin main
```
Before pushing: replace `<YOUR NAME>` in `README.md` and `LICENSE`, and add real
terminal screenshots/output of `scripts/demo.sh` under `docs/` (see README "Sample output").
After pushing, check the **Actions** tab shows a green CI build, then share the repo
link with your trainer for evaluation.
