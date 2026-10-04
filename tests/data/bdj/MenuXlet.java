// Menü der BD-J-Testdisc (tools/make_test_bdj.py): ein Feld mit zwei Schaltflächen über dem
// laufenden Film. Pfeiltasten wechseln die Auswahl, Enter blendet das Menü aus. Flächen statt
// Schrift, damit der Test ohne Schriftdateien auskommt und Farben an festen Stellen prüfen kann.
package lumentest;

import java.awt.Color;
import java.awt.Component;
import java.awt.Graphics;
import java.awt.event.KeyEvent;

import javax.tv.xlet.Xlet;
import javax.tv.xlet.XletContext;

import org.dvb.event.EventManager;
import org.dvb.event.UserEvent;
import org.dvb.event.UserEventListener;
import org.dvb.event.UserEventRepository;
import org.havi.ui.HScene;
import org.havi.ui.HSceneFactory;

public class MenuXlet implements Xlet, UserEventListener {
    static final Color PANEL = new Color(16, 24, 40, 200);
    static final Color BUTTON = new Color(96, 104, 120);
    static final Color SELECTED = new Color(255, 208, 0);

    private HScene scene;
    private int selected = 0;

    private final Component menu = new Component() {
        public void paint(Graphics g) {
            g.setColor(PANEL);
            g.fillRect(160, 240, 720, 600);
            for (int i = 0; i < 2; i++) {
                g.setColor(i == selected ? SELECTED : BUTTON);
                g.fillRect(240, 340 + i * 220, 560, 140);
            }
        }
    };

    public void initXlet(XletContext context) {
    }

    public void startXlet() {
        scene = HSceneFactory.getInstance().getDefaultHScene();
        scene.setBounds(0, 0, 1920, 1080);
        menu.setBounds(0, 0, 1920, 1080);
        scene.add(menu);
        scene.setVisible(true);
        scene.repaint();

        UserEventRepository keys = new UserEventRepository("menu");
        keys.addAllArrowKeys();
        keys.addKey(KeyEvent.VK_ENTER);
        EventManager.getInstance().addUserEventListener(this, keys);
    }

    public void pauseXlet() {
    }

    public void destroyXlet(boolean unconditional) {
        EventManager.getInstance().removeUserEventListener(this);
        if (scene != null) {
            scene.setVisible(false);
            HSceneFactory.getInstance().dispose(scene);
            scene = null;
        }
    }

    public void userEventReceived(UserEvent e) {
        if (e.getType() != KeyEvent.KEY_PRESSED || scene == null)
            return;
        switch (e.getCode()) {
        case KeyEvent.VK_UP:
        case KeyEvent.VK_DOWN:
            selected = 1 - selected;
            scene.repaint();
            break;
        case KeyEvent.VK_ENTER:
            // "Film starten": das Menü verschwindet, der Film läuft weiter
            scene.setVisible(false);
            break;
        default:
            break;
        }
    }
}
