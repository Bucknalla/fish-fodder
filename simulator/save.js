// "Save frame as PNG", for the simulator served by `fish-fodder serve`.
// Left out of the single-file build.

const button = document.getElementById('save');
const canvas = document.getElementById('panel');
button.hidden = false;
button.addEventListener('click', () => {
  canvas.toBlob((blob) => {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = "fish-fodder-frame.png";
    a.click();
    setTimeout(() => URL.revokeObjectURL(a.href), 1000);
  });
});
