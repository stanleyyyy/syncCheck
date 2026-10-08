// Drag, tap and pinch on a chart, with mouse, pen or touch (pointer events).
// Vertical touch swipes still scroll the page (touch-action: pan-y on the element).

export function bindGestures(element, { onStart, onDrag, onEnd, onTap, onPinchStart, onPinch }) {
	const pointers = new Map();
	let drag = null;	// { x, moved }
	let pinch = null;	// { distance }

	const distance = () => {
		const [a, b] = [...pointers.values()];
		return Math.hypot(a.x - b.x, a.y - b.y);
	};
	const center = () => {
		const [a, b] = [...pointers.values()];
		return (a.x + b.x) / 2;
	};

	element.addEventListener('pointerdown', (e) => {
		if (e.pointerType === 'mouse' && e.button !== 0) return;
		element.setPointerCapture(e.pointerId);
		pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
		if (pointers.size === 1) {
			drag = { x: e.clientX, moved: false };
			onStart?.(e);
		} else if (pointers.size === 2 && onPinch) {
			drag = null;
			pinch = { distance: distance() };
			onPinchStart?.(center());
		}
		e.preventDefault();
	});

	element.addEventListener('pointermove', (e) => {
		if (!pointers.has(e.pointerId)) return;
		pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
		if (pinch && pointers.size === 2) {
			onPinch(distance() / Math.max(pinch.distance, 1), center());
		} else if (drag) {
			const dx = e.clientX - drag.x;
			if (!drag.moved && Math.abs(dx) < 4) return;
			if (!drag.moved) element.classList.add('dragging');
			drag.moved = true;
			onDrag?.(dx, e);
		}
	});

	const finish = (e) => {
		if (!pointers.has(e.pointerId)) return;
		pointers.delete(e.pointerId);
		if (pointers.size > 0) return;
		element.classList.remove('dragging');
		const tapped = drag && !drag.moved;
		const moved = !tapped;
		drag = null;
		pinch = null;
		onEnd?.(moved);
		if (tapped && e.type === 'pointerup') onTap?.(e);
	};
	element.addEventListener('pointerup', finish);
	element.addEventListener('pointercancel', finish);
}
