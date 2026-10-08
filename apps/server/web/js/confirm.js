// Modal confirmation for destructive actions.
const $ = (id) => document.getElementById(id);

export function confirmAction({ title, message, confirm = 'Delete', danger = true }) {
	const dialog = $('confirmDialog');
	$('confirmTitle').textContent = title;
	$('confirmMessage').textContent = message;
	const ok = $('confirmOk');
	ok.textContent = confirm;
	ok.classList.toggle('danger', danger);
	return new Promise((resolve) => {
		const finish = (result) => {
			ok.removeEventListener('click', onOk);
			$('confirmCancel').removeEventListener('click', onCancel);
			dialog.removeEventListener('close', onCancel);
			if (dialog.open) dialog.close();
			resolve(result);
		};
		const onOk = () => finish(true);
		const onCancel = () => finish(false);
		ok.addEventListener('click', onOk);
		$('confirmCancel').addEventListener('click', onCancel);
		dialog.addEventListener('close', onCancel);
		dialog.showModal();
		$('confirmCancel').focus();
	});
}
