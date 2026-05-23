const { test, expect } = require('@playwright/test');

const node1Url = process.env.MMWAVE_NODE1_URL || 'http://10.0.107.148';
const node2Url = process.env.MMWAVE_NODE2_URL || 'http://10.0.107.149';

function encodeField(value) {
  return encodeURIComponent(String(value ?? ''));
}

function buildRoomConfigCommand(config) {
  return [
    config.roomId,
    config.sensorRole,
    config.poseX,
    config.poseY,
    config.heading,
    config.roomWidth,
    config.roomHeight
  ].map(encodeField).join('|');
}

async function fetchSnapshot(request, baseUrl) {
  const response = await request.get(`${baseUrl}/api/snapshot`);
  expect(response.ok()).toBeTruthy();
  return response.json();
}

async function postCommand(request, baseUrl, command) {
  let lastError = null;

  for (let attempt = 0; attempt < 4; attempt += 1) {
    try {
      const response = await request.post(`${baseUrl}/api/command`, {
        headers: { 'Content-Type': 'text/plain' },
        data: command,
        timeout: 30000
      });
      expect(response.ok()).toBeTruthy();
      return response.json();
    } catch (error) {
      lastError = error;
      await new Promise(resolve => setTimeout(resolve, 1500 * (attempt + 1)));
    }
  }

  throw lastError;
}

async function waitForSnapshotMatch(request, baseUrl, predicate, message) {
  await expect
    .poll(async () => {
      const snapshot = await fetchSnapshot(request, baseUrl);
      return predicate(snapshot) ? snapshot : null;
    }, { message })
    .not.toBeNull();
}

async function waitForLiveConnection(page) {
  await expect
    .poll(async () => page.locator('#connectionText').textContent())
    .toContain('Live WebSocket');
}

async function ensureSettingsVisible(page) {
  const settingsSection = page.locator('#settingsSection');
  if (await settingsSection.isVisible()) {
    return;
  }

  await page.click('#settingsToggleButton');
  await expect(settingsSection).toBeVisible();
}

async function selectEditorTarget(page, nodeId) {
  await page.selectOption('#roomEditorTargetInput', nodeId);
  await expect(page.locator('#roomEditorTargetInput')).toHaveValue(nodeId);
}

async function movePeerHandle(page, nodeId, mode, clientDeltaX, clientDeltaY = 0) {
  return page.evaluate(async ({ nodeId: targetNodeId, mode: targetMode, clientDeltaX: deltaX, clientDeltaY: deltaY }) => {
    const select = document.getElementById('roomEditorTargetInput');
    if (select.value !== targetNodeId) {
      select.value = targetNodeId;
      select.dispatchEvent(new Event('change', { bubbles: true }));
      await new Promise((resolve) => setTimeout(resolve, 500));
    }

    const canvas = document.getElementById('roomFusionCanvas');
    const handle = roomFusionLayout.interactiveNodes.find((node) => node.nodeId === targetNodeId && node.mode === targetMode);
    if (!handle) {
      throw new Error(`Missing handle ${targetMode} for ${targetNodeId}`);
    }

    const rect = canvas.getBoundingClientRect();
    const startClientX = rect.left + (handle.center.x / canvas.width) * rect.width;
    const startClientY = rect.top + (handle.center.y / canvas.height) * rect.height;
    const endClientX = startClientX + deltaX;
    const endClientY = startClientY + deltaY;

    beginRoomEditorInteraction({ clientX: startClientX, clientY: startClientY, pointerId: 99, preventDefault() {} });
    moveRoomEditorInteraction({ clientX: endClientX, clientY: endClientY, pointerId: 99, preventDefault() {} });

    const stateAfterMove = {
      poseX: document.getElementById('poseXInput')?.value,
      poseY: document.getElementById('poseYInput')?.value,
      heading: document.getElementById('headingInput')?.value,
      roomWidth: document.getElementById('roomWidthInput')?.value,
      roomHeight: document.getElementById('roomHeightInput')?.value
    };

    endRoomEditorInteraction({ clientX: endClientX, clientY: endClientY, pointerId: 99, preventDefault() {} });

    await new Promise((resolve) => setTimeout(resolve, 1700));

    return {
      ...stateAfterMove,
      roomEditorStatus: document.getElementById('roomEditorStatus')?.textContent || '',
      haSetupStatus: document.getElementById('haSetupStatus')?.textContent || ''
    };
  }, { nodeId, mode, clientDeltaX, clientDeltaY });
}

test.describe.serial('live room editor', () => {
  let baselineNode1;
  let baselineNode2;

  test.beforeAll(async ({ request }) => {
    baselineNode1 = await fetchSnapshot(request, node1Url);
    baselineNode2 = await fetchSnapshot(request, node2Url);
  });

  test('shows live connection and peer editor controls', async ({ page, request }) => {
    const node1Snapshot = await fetchSnapshot(request, node1Url);

    await page.goto(node1Url, { waitUntil: 'domcontentloaded' });
    await waitForLiveConnection(page);
    await ensureSettingsVisible(page);

    await expect(page.locator('#roomEditorTargetInput')).toBeVisible();
    await expect(page.locator('#roomWidthInput')).toHaveValue(String(node1Snapshot.room_width_cm));
    await expect(page.locator('#roomHeightInput')).toHaveValue(String(node1Snapshot.room_height_cm));
    await expect(page.locator('#roomEditorStatus')).toBeVisible();

    const options = await page.locator('#roomEditorTargetInput option').evaluateAll(nodes =>
      nodes.map(node => ({ value: node.value, text: node.textContent || '' }))
    );

    expect(options.some(option => option.value === '__local__')).toBeTruthy();
    expect(options.some(option => option.value === baselineNode2.node_id)).toBeTruthy();
  });

  test('publishes peer pose changes and receives applied acknowledgement', async ({ page, request }) => {
    const initialSnapshot = await fetchSnapshot(request, node2Url);

    await page.goto(node1Url, { waitUntil: 'domcontentloaded' });
    await waitForLiveConnection(page);
    await ensureSettingsVisible(page);
    await selectEditorTarget(page, baselineNode2.node_id);

    const result = await movePeerHandle(page, baselineNode2.node_id, 'move', 140, -40);

    expect(Number(result.poseX)).not.toBe(initialSnapshot.pose_x_cm);
    expect(Number(result.poseY)).not.toBe(initialSnapshot.pose_y_cm);
    expect(result.roomEditorStatus).toContain(`Published and applied on ${baselineNode2.node_id}`);

    await waitForSnapshotMatch(
      request,
      node2Url,
      snapshot => snapshot.pose_x_cm === Number(result.poseX) && snapshot.pose_y_cm === Number(result.poseY),
      'node 2 pose should match the peer editor values'
    );
  });

  test('publishes peer room envelope width changes and receives applied acknowledgement', async ({ page, request }) => {
    const initialSnapshot = await fetchSnapshot(request, node2Url);

    await page.goto(node1Url, { waitUntil: 'domcontentloaded' });
    await waitForLiveConnection(page);
    await ensureSettingsVisible(page);
    await selectEditorTarget(page, baselineNode2.node_id);

    const result = await movePeerHandle(page, baselineNode2.node_id, 'room-width', 220, 0);

    expect(Number(result.roomWidth)).toBeGreaterThan(initialSnapshot.room_width_cm);
    expect(result.roomEditorStatus).toContain(`Published and applied on ${baselineNode2.node_id}`);

    await waitForSnapshotMatch(
      request,
      node2Url,
      snapshot => snapshot.room_width_cm === Number(result.roomWidth) && snapshot.room_height_cm === Number(result.roomHeight),
      'node 2 room dimensions should match the peer editor values'
    );
  });
});